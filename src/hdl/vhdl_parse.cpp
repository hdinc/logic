#include "vhdl_parse.h"

#include <algorithm>
#include <cctype>

namespace logic::hdl {

namespace {

std::string lower(std::string s)
{
    for (auto& c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string trim(const std::string& s)
{
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// the source without comments, strings and character literals kept. the
// lower case copy has the same positions.
std::string strip_comments(const std::string& s)
{
    std::string r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '"') {
            // "..." with "" inside
            size_t j = i + 1;
            while (j < s.size()) {
                if (s[j] == '"') {
                    if (j + 1 < s.size() && s[j + 1] == '"') {
                        j += 2;
                        continue;
                    }
                    break;
                }
                j++;
            }
            r.append(s, i, j + 1 - i);
            i = j + 1;
        } else if (s[i] == '\'' && i + 2 < s.size() && s[i + 2] == '\'') {
            // a character literal, but not an attribute like x'range
            r.append(s, i, 3);
            i += 3;
        } else if (s.compare(i, 2, "--") == 0) {
            while (i < s.size() && s[i] != '\n')
                i++;
        } else if (s.compare(i, 2, "/*") == 0) {
            auto e = s.find("*/", i + 2);
            i = e == std::string::npos ? s.size() : e + 2;
            r += ' ';
        } else {
            r += s[i++];
        }
    }
    return r;
}

bool is_word_char(char c)
{
    return std::isalnum((unsigned char)c) || c == '_';
}

// position of the word in lower case text from `from`, npos if not found
size_t find_word(const std::string& text, const std::string& word, size_t from = 0)
{
    for (size_t p = text.find(word, from); p != std::string::npos; p = text.find(word, p + 1)) {
        bool start = p == 0 || !is_word_char(text[p - 1]);
        bool end = p + word.size() >= text.size() || !is_word_char(text[p + word.size()]);
        if (start && end) return p;
    }
    return std::string::npos;
}

size_t skip_space(const std::string& s, size_t i)
{
    while (i < s.size() && std::isspace((unsigned char)s[i]))
        i++;
    return i;
}

// the position after the parenthesis matching the one at open
size_t matching(const std::string& s, size_t open)
{
    int depth = 0;
    bool in_string = false;
    for (size_t i = open; i < s.size(); i++) {
        char c = s[i];
        if (in_string) {
            if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') in_string = true;
        else if (c == '(') depth++;
        else if (c == ')' && --depth == 0) return i + 1;
    }
    return std::string::npos;
}

// splits at the separator outside parentheses and strings
std::vector<std::string> split_top(const std::string& s, char sep)
{
    std::vector<std::string> r;
    std::string cur;
    int depth = 0;
    bool in_string = false;
    for (char c : s) {
        if (in_string) {
            if (c == '"') in_string = false;
        } else if (c == '"') {
            in_string = true;
        } else if (c == '(') {
            depth++;
        } else if (c == ')') {
            depth--;
        } else if (c == sep && depth == 0) {
            r.push_back(cur);
            cur.clear();
            continue;
        }
        cur += c;
    }
    if (!trim(cur).empty()) r.push_back(cur);
    return r;
}

// the position of ":=" outside parentheses and strings, npos if none
size_t find_default(const std::string& s)
{
    int depth = 0;
    bool in_string = false;
    for (size_t i = 0; i + 1 < s.size(); i++) {
        char c = s[i];
        if (in_string) {
            if (c == '"') in_string = false;
        } else if (c == '"') {
            in_string = true;
        } else if (c == '(') {
            depth++;
        } else if (c == ')') {
            depth--;
        } else if (depth == 0 && c == ':' && s[i + 1] == '=') {
            return i;
        }
    }
    return std::string::npos;
}

// "generic (...)" or "port (...)" in the entity header from i. empty if it
// doesn't start there.
bool clause(const std::string& text, const std::string& low, size_t& i, const char* keyword, std::string& inside)
{
    i = skip_space(text, i);
    std::string k = keyword;
    if (low.compare(i, k.size(), k) != 0 || (i + k.size() < low.size() && is_word_char(low[i + k.size()]))) return false;
    size_t open = skip_space(text, i + k.size());
    if (open >= text.size() || text[open] != '(') return false;
    size_t close = matching(text, open);
    if (close == std::string::npos) return false;
    inside = text.substr(open + 1, close - open - 2);
    i = skip_space(text, close);
    if (i < text.size() && text[i] == ';') i++;
    return true;
}

}

bool parse_vhdl_entity(const std::string& source, const std::string& name, vhdl_entity& out, std::string& error)
{
    auto text = strip_comments(source);
    auto low = lower(text);
    auto want = lower(name);

    // "entity <name> is"
    size_t at = std::string::npos;
    for (size_t p = find_word(low, "entity"); p != std::string::npos; p = find_word(low, "entity", p + 1)) {
        size_t n = skip_space(low, p + 6);
        size_t e = n;
        while (e < low.size() && is_word_char(low[e]))
            e++;
        if (low.substr(n, e - n) != want) continue;
        size_t is = skip_space(low, e);
        if (low.compare(is, 2, "is") == 0 && (is + 2 >= low.size() || !is_word_char(low[is + 2]))) {
            at = is + 2;
            out.name = text.substr(n, e - n);
            // the library and use clauses before it
            out.context.clear();
            for (size_t c = 0; c < p;) {
                size_t lib = find_word(low, "library", c), use = find_word(low, "use", c);
                size_t start = std::min(lib, use);
                if (start >= p) break;
                size_t end = low.find(';', start);
                if (end == std::string::npos || end >= p) break;
                out.context += trim(text.substr(start, end - start + 1)) + "\n";
                c = end + 1;
            }
            break;
        }
    }
    if (at == std::string::npos) return false;

    out.generics.clear();
    out.ports.clear();
    size_t i = at;
    std::string inside;

    if (clause(text, low, i, "generic", inside)) {
        for (auto& item : split_top(inside, ';')) {
            auto colon = item.find(':');
            if (colon == std::string::npos) {
                error = "can't read the generic '" + trim(item) + "'";
                return false;
            }
            auto rest = item.substr(colon + 1);
            auto def_at = find_default(rest);
            std::string type = trim(def_at == std::string::npos ? rest : rest.substr(0, def_at));
            std::string def = def_at == std::string::npos ? "" : trim(rest.substr(def_at + 2));
            auto names = item.substr(0, colon);
            // "constant" may come before the names
            auto lnames = lower(trim(names));
            if (lnames.rfind("constant ", 0) == 0) names = trim(names).substr(9);
            for (auto& n : split_top(names, ','))
                out.generics.push_back({trim(n), type, def});
        }
    }

    if (clause(text, low, i, "port", inside)) {
        for (auto& item : split_top(inside, ';')) {
            auto colon = item.find(':');
            if (colon == std::string::npos) {
                error = "can't read the port '" + trim(item) + "'";
                return false;
            }
            auto rest = trim(item.substr(colon + 1));
            auto def_at = find_default(rest);
            if (def_at != std::string::npos) rest = trim(rest.substr(0, def_at));
            std::string mode = "in";
            auto lrest = lower(rest);
            for (auto m : {"inout", "in", "out", "buffer", "linkage"}) {
                std::string mm = m;
                if (lrest.compare(0, mm.size(), mm) == 0 && (lrest.size() == mm.size() || !is_word_char(lrest[mm.size()]))) {
                    mode = mm;
                    rest = trim(rest.substr(mm.size()));
                    break;
                }
            }
            auto names = trim(item.substr(0, colon));
            if (lower(names).rfind("signal ", 0) == 0) names = trim(names.substr(7));
            for (auto& n : split_top(names, ','))
                out.ports.push_back({trim(n), mode, rest});
        }
    }
    return true;
}

}
