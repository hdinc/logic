#include "format.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace logic::wave {

namespace {

// multiplies the words by m and adds a, with 32 bit halves so it doesn't
// need a 128 bit type
void mul_add(std::vector<uint64_t>& words, uint32_t m, uint32_t a)
{
    uint64_t carry = a;
    for (auto& w : words) {
        uint64_t lo = (w & 0xFFFFFFFF) * m + carry;
        uint64_t hi = (w >> 32) * m + (lo >> 32);
        w = (hi << 32) | (lo & 0xFFFFFFFF);
        carry = hi >> 32;
    }
}

std::string decimal(const sim::value& v)
{
    // base 10^9 digits, consuming 32 bits at a time. wide buses keep all
    // their bits without arithmetic per character on every bit.
    constexpr uint64_t base = 1000000000;
    std::vector<uint32_t> digits{0};
    for (int word = (v.width() + 31) / 32 - 1; word >= 0; word--) {
        uint64_t carry = uint32_t(v.bits(word / 2) >> ((word % 2) * 32));
        for (auto& digit : digits) {
            uint64_t n = (uint64_t(digit) << 32) + carry;
            digit = uint32_t(n % base);
            carry = n / base;
        }
        while (carry) {
            digits.push_back(uint32_t(carry % base));
            carry /= base;
        }
    }
    std::string result = std::to_string(digits.back());
    for (int k = (int)digits.size() - 2; k >= 0; k--) {
        auto group = std::to_string(digits[k]);
        result.append(9 - group.size(), '0');
        result += group;
    }
    return result;
}

}

const char* format_id(format f)
{
    static const char* ids[] = {"hex", "uint", "sint", "binary", "octal"};
    return ids[int(f)];
}

std::optional<format> parse_format_id(std::string_view s)
{
    for (int f = 0; f < 5; f++)
        if (s == format_id(format(f))) return format(f);
    return std::nullopt;
}

std::string format_value(const sim::value& v, format f)
{
    if (v.width() == 0) return "-";
    if (f == format::uint || f == format::sint) {
        if (!v.fully_known()) return sim::to_string(v);
        if (f == format::sint && v.get(v.width() - 1) == sim::bit_state::one) {
            auto magnitude = sim::known(0, v.width());
            bool carry = true;
            for (int k = 0; k < v.width(); k++) {
                bool inverted = v.get(k) != sim::bit_state::one;
                magnitude.set(k, inverted != carry ? sim::bit_state::one : sim::bit_state::zero);
                carry = inverted && carry;
            }
            return "-" + decimal(magnitude);
        }
        return decimal(v);
    }
    if (v.width() == 1) return sim::to_string(v);
    int group = f == format::hex ? 4 : f == format::octal ? 3 : 1;
    std::string result;
    for (int first = ((v.width() - 1) / group) * group; first >= 0; first -= group) {
        int digit = 0;
        bool unknown = false, floating = true;
        for (int k = 0; k < group && first + k < v.width(); k++) {
            auto state = v.get(first + k);
            if (state == sim::bit_state::one) digit |= 1 << k;
            unknown |= state == sim::bit_state::x || state == sim::bit_state::z;
            floating &= state == sim::bit_state::z;
        }
        result += floating ? 'z' : unknown ? 'x' : "0123456789ABCDEF"[digit];
    }
    return (group == 4 ? "0x" : group == 3 ? "0o" : "0b") + result;
}

std::string format_real(double v)
{
    if (std::isnan(v)) return "x";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

std::string format_change(const trace& t, size_t i, format f)
{
    switch (t.type()) {
    case kind::digital:
        return format_value(t.value(i), f);
    case kind::real:
        return format_real(t.real(i));
    case kind::text:
        return t.text(i);
    }
    return {};
}

bool pattern::matches(const trace& t, size_t i) const
{
    switch (type) {
    case kind::digital: {
        if (t.type() != kind::digital) return false;
        auto w = t.value(i);
        if (w.width() != v.width()) return false;
        for (int k = 0; k < v.words(); k++) {
            uint64_t diff = (w.bits(k) ^ v.bits(k)) | (w.x(k) ^ v.x(k)) | (w.z(k) ^ v.z(k));
            if (diff & care[k]) return false;
        }
        return true;
    }
    case kind::real: {
        if (t.type() != kind::real) return false;
        double a = t.real(i);
        return std::abs(a - real) <= 1e-12 * std::max(1.0, std::abs(real));
    }
    case kind::text:
        return t.type() == kind::text && t.text(i) == text;
    }
    return false;
}

std::optional<pattern> parse_pattern(std::string_view text, kind k, int width, format f)
{
    std::string s;
    for (char c : text)
        if (!std::isspace((unsigned char)c) && c != '_') s += (char)std::tolower((unsigned char)c);
    pattern p;
    p.type = k;
    if (k == kind::text) {
        p.text = std::string(text);
        return p;
    }
    if (s.empty()) return std::nullopt;
    if (k == kind::real) {
        char* end = nullptr;
        p.real = std::strtod(s.c_str(), &end);
        if (end != s.c_str() + s.size()) return std::nullopt;
        return p;
    }

    int words = sim::value::word_count(width);
    std::vector<uint64_t> bits(words), x(words), z(words);
    p.care.assign(words, 0);
    for (int w = 0; w < words; w++)
        p.care[w] = sim::word_mask(width, w);

    int radix = f == format::hex ? 16 : f == format::binary ? 2 : f == format::octal ? 8 : 10;
    bool negative = false;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'b' || s[1] == 'o')) {
        radix = s[1] == 'x' ? 16 : s[1] == 'b' ? 2 : 8;
        s = s.substr(2);
    } else if (radix == 10 && s[0] == '-') {
        negative = true;
        s = s.substr(1);
    }
    if (s.empty()) return std::nullopt;

    if (radix == 10) {
        for (char c : s) {
            if (c < '0' || c > '9') return std::nullopt;
            mul_add(bits, 10, uint32_t(c - '0'));
        }
        if (negative) {
            for (auto& w : bits)
                w = ~w;
            mul_add(bits, 1, 1);
        }
    } else {
        int per_digit = radix == 16 ? 4 : radix == 8 ? 3 : 1;
        int pos = 0;
        for (int i = (int)s.size() - 1; i >= 0; i--, pos += per_digit) {
            char c = s[i];
            int digit = -1;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            if (digit >= radix) return std::nullopt;
            for (int b = 0; b < per_digit; b++) {
                int bit = pos + b;
                if (bit >= width) {
                    // more digits than bits: the extra ones must be 0
                    if (digit > 0 && (digit >> b) & 1) return std::nullopt;
                    continue;
                }
                uint64_t m = uint64_t(1) << (bit % 64);
                int w = bit / 64;
                if (c == 'x') x[w] |= m;
                else if (c == 'z') z[w] |= m;
                else if (c == '?') p.care[w] &= ~m;
                else if (digit < 0) return std::nullopt;
                else if ((digit >> b) & 1) bits[w] |= m;
            }
        }
    }

    p.v = sim::value(width, 0);
    for (int w = 0; w < words; w++)
        p.v.set_word(w, bits[w] & sim::word_mask(width, w), x[w], z[w]);
    return p;
}

}
