#include "expr.h"

#include <algorithm>
#include <cctype>
#include <limits>

namespace logic::model {

namespace {

bool is_function(const std::string& n)
{
    return n == "clog2" || n == "min" || n == "max";
}

// recursive descent over the text, the first error stops it
struct parser {
    const std::string& s;
    const param_values* scope = nullptr;
    std::vector<std::string>* names = nullptr;
    size_t at = 0;
    std::string error;

    parser(const std::string& text, const param_values* sc, std::vector<std::string>* n = nullptr)
        : s(text)
        , scope(sc)
        , names(n)
    {
    }

    void skip()
    {
        while (at < s.size() && std::isspace((unsigned char)s[at]))
            at++;
    }

    bool eat(char ch)
    {
        skip();
        if (at < s.size() && s[at] == ch) {
            at++;
            return true;
        }
        return false;
    }

    bool fail(std::string e)
    {
        if (error.empty()) error = std::move(e);
        return false;
    }

    // false on an overflow of the operation
    bool checked(bool overflow) { return !overflow || fail("the value is too large"); }

    // sum := term (('+' | '-') term)*
    bool sum(int64_t& v)
    {
        if (!term(v)) return false;
        for (;;) {
            int64_t r;
            if (eat('+')) {
                if (!term(r) || !checked(__builtin_add_overflow(v, r, &v))) return false;
            } else if (eat('-')) {
                if (!term(r) || !checked(__builtin_sub_overflow(v, r, &v))) return false;
            } else {
                return true;
            }
        }
    }

    // term := unary (('*' | '/' | '%') unary)*
    bool term(int64_t& v)
    {
        if (!unary(v)) return false;
        for (;;) {
            int64_t r;
            if (eat('*')) {
                if (!unary(r) || !checked(__builtin_mul_overflow(v, r, &v))) return false;
            } else if (eat('/')) {
                if (!unary(r)) return false;
                if (r == 0) return fail("division by zero");
                if (!checked(v == std::numeric_limits<int64_t>::min() && r == -1)) return false;
                v /= r;
            } else if (eat('%')) {
                if (!unary(r)) return false;
                if (r == 0) return fail("division by zero");
                v = r == -1 ? 0 : v % r;
            } else {
                return true;
            }
        }
    }

    // unary := '-' unary | '+' unary | primary
    bool unary(int64_t& v)
    {
        if (eat('-')) {
            if (!unary(v)) return false;
            return checked(__builtin_sub_overflow(int64_t(0), v, &v));
        }
        if (eat('+')) return unary(v);
        return primary(v);
    }

    bool primary(int64_t& v)
    {
        skip();
        if (at >= s.size()) return fail("the expression ends early");
        char ch = s[at];
        if (std::isdigit((unsigned char)ch)) {
            v = 0;
            while (at < s.size() && std::isdigit((unsigned char)s[at]))
                if (__builtin_mul_overflow(v, int64_t(10), &v) || __builtin_add_overflow(v, int64_t(s[at++] - '0'), &v)) return fail("the value is too large");
            return true;
        }
        if (std::isalpha((unsigned char)ch) || ch == '_') {
            auto start = at;
            while (at < s.size() && (std::isalnum((unsigned char)s[at]) || s[at] == '_'))
                at++;
            auto name = s.substr(start, at - start);
            if (is_function(name)) return call(name, v);
            if (names && std::find(names->begin(), names->end(), name) == names->end()) names->push_back(name);
            if (!scope) {
                v = 0;
                return true;
            }
            auto it = scope->find(name);
            if (it == scope->end() || !std::holds_alternative<int64_t>(it->second)) return fail("unknown name '" + name + "'");
            v = std::get<int64_t>(it->second);
            return true;
        }
        if (eat('(')) {
            if (!sum(v)) return false;
            if (!eat(')')) return fail("')' is missing");
            return true;
        }
        return fail(std::string("unexpected '") + ch + "'");
    }

    bool call(const std::string& name, int64_t& v)
    {
        if (!eat('(')) return fail("'(' is missing after " + name);
        std::vector<int64_t> args;
        if (!eat(')')) {
            do {
                int64_t a;
                if (!sum(a)) return false;
                args.push_back(a);
            } while (eat(','));
            if (!eat(')')) return fail("')' is missing");
        }
        size_t want = name == "clog2" ? 1 : 2;
        if (args.size() != want) return fail(name + " takes " + std::to_string(want) + (want == 1 ? " argument" : " arguments"));
        if (!scope) {
            v = 0;
            return true;
        }
        if (name == "min") {
            v = std::min(args[0], args[1]);
        } else if (name == "max") {
            v = std::max(args[0], args[1]);
        } else {
            // bits to count 0 .. x - 1
            if (args[0] < 0) return fail("clog2 of a negative value");
            v = 0;
            while (v < 63 && (int64_t(1) << v) < args[0])
                v++;
        }
        return true;
    }

    bool whole(int64_t& v)
    {
        if (!sum(v)) return false;
        skip();
        if (at < s.size()) return fail(std::string("unexpected '") + s[at] + "'");
        return true;
    }
};

}

std::optional<int64_t> eval_expr(const std::string& text, const param_values& scope, std::string& error)
{
    parser p(text, &scope);
    int64_t v = 0;
    if (!p.whole(v)) {
        error = p.error;
        return std::nullopt;
    }
    return v;
}

bool is_literal(const std::string& text)
{
    size_t a = 0, b = text.size();
    while (a < b && std::isspace((unsigned char)text[a]))
        a++;
    while (b > a && std::isspace((unsigned char)text[b - 1]))
        b--;
    if (a < b && text[a] == '-') a++;
    if (a == b) return false;
    return std::all_of(text.begin() + a, text.begin() + b, [](char ch) { return std::isdigit((unsigned char)ch) != 0; });
}

std::vector<std::string> expr_names(const std::string& text)
{
    std::vector<std::string> names;
    parser p(text, nullptr, &names);
    int64_t v;
    p.whole(v);
    return names;
}

bool valid_param_name(const std::string& name)
{
    if (name.empty() || std::isdigit((unsigned char)name[0])) return false;
    if (!std::all_of(name.begin(), name.end(), [](char ch) { return std::isalnum((unsigned char)ch) || ch == '_'; })) return false;
    return !is_function(name);
}

}
