#include "vcd.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace logic::wave {

namespace {

struct string_hash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
};

struct unit_name {
    std::string_view name;
    int exponent;
};
constexpr unit_name units[] = {{"s", 0}, {"ms", -3}, {"us", -6}, {"ns", -9}, {"ps", -12}, {"fs", -15}};

class parser {
public:
    parser(std::string_view text, load_progress* progress)
        : s(text)
        , progress(progress)
    {
    }

    trace_set parse()
    {
        header();
        body();
        return std::move(set);
    }

private:
    [[noreturn]] void fail(const std::string& message) const
    {
        throw std::runtime_error("line " + std::to_string(line) + ": " + message);
    }

    // the next run of non-space characters, empty at the end
    std::string_view token()
    {
        while (pos < s.size() && std::isspace((unsigned char)s[pos])) {
            if (s[pos] == '\n') line++;
            pos++;
        }
        size_t start = pos;
        while (pos < s.size() && !std::isspace((unsigned char)s[pos]))
            pos++;
        return s.substr(start, pos - start);
    }

    // the tokens up to $end, joined
    std::string until_end()
    {
        std::string text;
        while (true) {
            auto t = token();
            if (t.empty()) fail("missing $end");
            if (t == "$end") return text;
            text += t;
        }
    }

    void timescale(const std::string& text)
    {
        size_t k = 0;
        while (k < text.size() && std::isdigit((unsigned char)text[k]))
            k++;
        auto number = text.substr(0, k);
        auto unit = std::string_view(text).substr(k);
        if (number != "1" && number != "10" && number != "100") fail("unsupported timescale " + text);
        for (auto& u : units) {
            if (u.name == unit) {
                set.tb.exponent = u.exponent;
                multiplier = number == "1" ? 1 : number == "10" ? 10 : 100;
                return;
            }
        }
        fail("unsupported timescale " + text);
    }

    void var()
    {
        auto type = token();
        auto size = token();
        auto id = token();
        std::string ref(token());
        if (type.empty() || size.empty() || id.empty() || ref.empty() || ref == "$end") fail("incomplete $var");
        // a bit range is a separate token for most writers
        for (auto t = token(); t != "$end"; t = token()) {
            if (t.empty()) fail("missing $end");
            ref += t;
        }

        signal sig;
        sig.key = (int64_t)set.signals.size();
        for (auto& scope : scopes)
            sig.name += scope + ".";
        sig.name += ref;
        if (type == "real" || type == "realtime" || type == "shortreal") {
            sig.type = kind::real;
            sig.width = 64;
        } else if (type == "string") {
            sig.type = kind::text;
            sig.width = 1;
        } else {
            sig.type = kind::digital;
            sig.width = std::max(1, std::atoi(std::string(size).c_str()));
        }

        auto found = ids.find(id);
        if (found != ids.end()) {
            sig.trace = found->second;
            auto& t = set.traces[sig.trace];
            if (t.type() != sig.type || t.width() != sig.width) fail("identifier " + std::string(id) + " declared with different types");
        } else {
            sig.trace = (int)set.traces.size();
            set.traces.emplace_back(sig.type, sig.width);
            ids.emplace(std::string(id), sig.trace);
        }
        set.signals.push_back(std::move(sig));
    }

    void header()
    {
        while (true) {
            auto t = token();
            if (t.empty()) fail("no $enddefinitions");
            if (t == "$enddefinitions") {
                until_end();
                return;
            }
            if (t == "$timescale") timescale(until_end());
            else if (t == "$scope") {
                token();
                auto name = token();
                if (name.empty() || name == "$end") fail("incomplete $scope");
                until_end();
                scopes.emplace_back(name);
            } else if (t == "$upscope") {
                until_end();
                if (!scopes.empty()) scopes.pop_back();
            } else if (t == "$var") var();
            else if (t[0] == '$') until_end();
            else fail("unexpected " + std::string(t) + " in the header");
        }
    }

    trace& lookup(std::string_view id)
    {
        auto found = ids.find(id);
        if (found == ids.end()) fail("unknown identifier " + std::string(id));
        return set.traces[found->second];
    }

    static void set_bit(uint64_t* d, int words, int b, char c)
    {
        uint64_t m = uint64_t(1) << (b % 64);
        int w = b / 64;
        switch (c) {
        case '1':
        case 'h':
        case 'H':
            d[w] |= m;
            break;
        case '0':
        case 'l':
        case 'L':
            break;
        case 'z':
        case 'Z':
            d[2 * words + w] |= m;
            break;
        default:
            // x, u, w and -
            d[words + w] |= m;
            break;
        }
    }

    // vcd vectors may leave out leading digits: 0 and 1 extend with 0, x
    // and z with themselves
    sim::value vector(std::string_view digits, int width)
    {
        sim::value v(width, 0);
        auto d = v.data();
        int words = v.words();
        size_t len = digits.size();
        char first = len ? digits[0] : '0';
        char ext = std::strchr("xXzZuUwW-", first) ? first : '0';
        for (int b = 0; b < width; b++)
            set_bit(d, words, b, b < (int)len ? digits[len - 1 - b] : ext);
        v.normalize();
        return v;
    }

    void dumpoff()
    {
        for (auto& t : set.traces) {
            switch (t.type()) {
            case kind::digital:
                t.push(now, sim::unknown(t.width()));
                break;
            case kind::real:
                t.push(now, std::numeric_limits<double>::quiet_NaN());
                break;
            case kind::text:
                break;
            }
        }
    }

    void body()
    {
        bool timed = false;
        size_t count = 0;
        while (true) {
            auto t = token();
            if (t.empty()) break;
            if (progress && ++count % 65536 == 0) {
                progress->fraction = float(pos) / float(std::max<size_t>(1, s.size()));
                if (progress->cancel) throw std::runtime_error("cancelled");
            }

            char c = t[0];
            if (c == '#') {
                char* end = nullptr;
                std::string digits(t.substr(1));
                auto value = std::strtoull(digits.c_str(), &end, 10);
                if (digits.empty() || *end) fail("bad time " + std::string(t));
                time next = value * multiplier;
                if (timed && next < now) fail("time goes backwards");
                now = next;
                if (!timed) set.begin = now;
                timed = true;
                set.end = std::max(set.end, now);
            } else if (c == '$') {
                if (t == "$dumpoff") dumpoff();
                else if (t == "$comment") until_end();
                else if (t == "$dumpports") fail("extended vcd isn't supported");
                // $dumpvars, $dumpall, $dumpon and their $end only mark
                // where values are
            } else if (c == 'b' || c == 'B') {
                auto digits = t.substr(1);
                auto& tr = lookup(token());
                if (tr.type() == kind::digital) tr.push(now, vector(digits, tr.width()));
            } else if (c == 'r' || c == 'R') {
                std::string number(t.substr(1));
                auto& tr = lookup(token());
                char* end = nullptr;
                double v = std::strtod(number.c_str(), &end);
                if (*end) fail("bad real " + number);
                if (tr.type() == kind::real) tr.push(now, v);
            } else if (c == 's' || c == 'S') {
                std::string text(t.substr(1));
                auto& tr = lookup(token());
                if (tr.type() == kind::text) tr.push(now, std::move(text));
            } else if (std::strchr("01xXzZuUwWlLhH-", c)) {
                auto& tr = lookup(t.substr(1));
                if (tr.type() == kind::digital) tr.push(now, vector(t.substr(0, 1), tr.width()));
            } else {
                fail("unexpected " + std::string(t));
            }
        }
        if (progress) progress->fraction = 1;
    }

    std::string_view s;
    load_progress* progress;
    size_t pos = 0;
    int line = 1;

    trace_set set;
    std::vector<std::string> scopes;
    std::unordered_map<std::string, int, string_hash, std::equal_to<>> ids;
    time multiplier = 1;
    time now = 0;
};

std::string identifier(int n)
{
    std::string id;
    do {
        id += char('!' + n % 94);
        n /= 94;
    } while (n > 0);
    return id;
}

std::string clean(std::string_view name)
{
    std::string s(name);
    for (auto& c : s)
        if (std::isspace((unsigned char)c)) c = '_';
    return s;
}

struct scope_node {
    std::map<std::string, scope_node> children;
    std::vector<int> signals;
};

}

trace_set load_vcd(const std::filesystem::path& file, load_progress* progress)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("can't open " + file.string());
    std::string text;
    in.seekg(0, std::ios::end);
    text.resize(size_t(in.tellg()));
    in.seekg(0);
    in.read(text.data(), (std::streamsize)text.size());
    if (!in) throw std::runtime_error("can't read " + file.string());
    return parse_vcd(text, progress);
}

trace_set parse_vcd(std::string_view text, load_progress* progress)
{
    return parser(text, progress).parse();
}

void write_vcd(std::ostream& out, const trace_set& set)
{
    out << "$version\n\tlogic\n$end\n";
    const char* unit = "ps";
    for (auto& u : units)
        if (u.exponent == set.tb.exponent) unit = u.name.data();
    out << "$timescale 1" << unit << " $end\n";

    scope_node root;
    for (int k = 0; k < (int)set.signals.size(); k++) {
        auto& name = set.signals[k].name;
        auto* node = &root;
        size_t start = 0;
        while (true) {
            auto sep = name.find_first_of("/.", start);
            if (sep == std::string::npos) break;
            node = &node->children[clean(std::string_view(name).substr(start, sep - start))];
            start = sep + 1;
        }
        node->signals.push_back(k);
    }

    auto write_vars = [&](const scope_node& node) {
        for (int k : node.signals) {
            auto& sig = set.signals[k];
            auto leaf = clean(sig.leaf());
            std::string range;
            if (auto b = leaf.find('['); b != std::string::npos) {
                range = " " + leaf.substr(b);
                leaf = leaf.substr(0, b);
            } else if (sig.type == kind::digital && sig.width > 1) {
                range = " [" + std::to_string(sig.width - 1) + ":0]";
            }
            const char* type = sig.type == kind::real ? "real" : sig.type == kind::text ? "string" : "wire";
            int width = sig.type == kind::real ? 64 : sig.width;
            out << "$var " << type << " " << width << " " << identifier(sig.trace) << " " << leaf << range << " $end\n";
        }
    };
    auto write_scope = [&](auto& self, const std::string& name, const scope_node& node) -> void {
        out << "$scope module " << name << " $end\n";
        write_vars(node);
        for (auto& [child, n] : node.children)
            self(self, child, n);
        out << "$upscope $end\n";
    };
    if (!root.signals.empty()) write_scope(write_scope, "top", {{}, root.signals});
    for (auto& [child, n] : root.children)
        write_scope(write_scope, child, n);
    out << "$enddefinitions $end\n";

    // the traces in use, each written once
    std::vector<int> used;
    for (auto& sig : set.signals)
        if (std::find(used.begin(), used.end(), sig.trace) == used.end()) used.push_back(sig.trace);
    struct change {
        time t;
        int trace;
        size_t i;
    };
    std::vector<change> changes;
    for (int tr : used)
        for (size_t i = 0; i < set.traces[tr].size(); i++)
            changes.push_back({set.traces[tr].at(i), tr, i});
    std::stable_sort(changes.begin(), changes.end(), [](auto& a, auto& b) { return a.t < b.t; });

    bool first = true;
    time last = 0;
    for (auto& c : changes) {
        if (first || c.t != last) out << "#" << c.t << "\n";
        first = false;
        last = c.t;
        auto& tr = set.traces[c.trace];
        auto id = identifier(c.trace);
        switch (tr.type()) {
        case kind::digital: {
            std::string bits;
            for (int b = tr.width() - 1; b >= 0; b--)
                bits += "01xz"[int(tr.bit(c.i, b))];
            if (tr.width() == 1) out << bits << id << "\n";
            else out << "b" << bits << " " << id << "\n";
            break;
        }
        case kind::real: {
            double v = tr.real(c.i);
            if (std::isnan(v)) break;
            char buf[40];
            std::snprintf(buf, sizeof(buf), "r%.17g ", v);
            out << buf << id << "\n";
            break;
        }
        case kind::text:
            out << "s" << clean(tr.text(c.i)) << " " << id << "\n";
            break;
        }
    }
    if (set.end > last || first) out << "#" << set.end << "\n";
}

}
