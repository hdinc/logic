#include "numbers.h"
#include "../wave/format.h"
#include "arith.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace logic::sim {

namespace {

using model::number_format;

// a gcc and clang extension, for fixed point fractions
__extension__ typedef unsigned __int128 u128;

// ---- floats ----

float half_to_float(uint16_t h)
{
    uint32_t sign = uint32_t(h >> 15) << 31;
    int exp = (h >> 10) & 0x1F;
    uint32_t mant = h & 0x3FF;
    uint32_t bits;
    if (exp == 0x1F) {
        bits = sign | 0x7F800000 | (mant << 13);
    } else if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {
            // subnormal: normalize it
            int e = -1;
            do {
                e++;
                mant <<= 1;
            } while (!(mant & 0x400));
            bits = sign | uint32_t(127 - 15 - e) << 23 | (mant & 0x3FF) << 13;
        }
    } else {
        bits = sign | uint32_t(exp - 15 + 127) << 23 | mant << 13;
    }
    return std::bit_cast<float>(bits);
}

// rounding to the nearest, ties to even
uint16_t float_to_half(float f)
{
    uint32_t x = std::bit_cast<uint32_t>(f);
    uint16_t sign = uint16_t((x >> 16) & 0x8000);
    int exp = int((x >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = x & 0x7FFFFF;

    if (((x >> 23) & 0xFF) == 0xFF) return sign | 0x7C00 | (mant ? 0x200 : 0);
    if (exp >= 0x1F) return sign | 0x7C00;
    if (exp <= 0) {
        // subnormal or 0: the mantissa with its hidden bit, shifted down
        if (exp < -10) return sign;
        mant |= 0x800000;
        int shift = 14 - exp;
        uint32_t half = mant >> shift;
        uint32_t rest = mant & ((1u << shift) - 1);
        uint32_t mid = 1u << (shift - 1);
        if (rest > mid || (rest == mid && (half & 1))) half++;
        return sign | uint16_t(half);
    }
    uint32_t half = uint32_t(exp) << 10 | mant >> 13;
    uint32_t rest = mant & 0x1FFF;
    // a carry into the exponent is right, up to infinity
    if (rest > 0x1000 || (rest == 0x1000 && (half & 1))) half++;
    return sign | uint16_t(half);
}

uint16_t float_to_bfloat16(float f)
{
    uint32_t x = std::bit_cast<uint32_t>(f);
    if ((x & 0x7F800000) == 0x7F800000 && (x & 0x7FFFFF)) return uint16_t(x >> 16) | 0x40;
    x += 0x7FFF + ((x >> 16) & 1);
    return uint16_t(x >> 16);
}

// the shortest text that reads back as the same number
template <class T>
std::string shortest(T v)
{
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v < 0 ? "-inf" : "inf";
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof(buf), v);
    return std::string(buf, r.ptr);
}

// ---- fixed point ----

// |v| as integer and fraction bits, exactly in decimal
std::string fixed_text(uint64_t magnitude, bool negative, int frac)
{
    uint64_t whole = frac >= 64 ? 0 : magnitude >> frac;
    u128 rest = frac >= 64 ? magnitude : magnitude & ((uint64_t(1) << frac) - 1);
    std::string s = (negative ? "-" : "") + std::to_string(whole);
    if (rest == 0) return s;
    s += '.';
    // each step takes the next decimal digit of rest / 2^frac
    while (rest) {
        rest *= 10;
        s += char('0' + int(rest >> frac));
        rest &= (u128(1) << frac) - 1;
    }
    return s;
}

std::string trim(std::string_view s)
{
    while (!s.empty() && std::isspace((unsigned char)s.front()))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace((unsigned char)s.back()))
        s.remove_suffix(1);
    return std::string(s);
}

// ---- integers ----

std::optional<value> parse_integer(std::string s, int width, int radix, bool allow_negative)
{
    bool negative = false;
    if (!s.empty() && (s[0] == '-' || s[0] == '+')) {
        negative = s[0] == '-';
        s.erase(0, 1);
    }
    if (negative && !allow_negative) return std::nullopt;
    if (s.size() > 2 && s[0] == '0') {
        char p = (char)std::tolower((unsigned char)s[1]);
        if (p == 'x') radix = 16;
        if (p == 'b') radix = 2;
        if (p == 'o') radix = 8;
        if (p == 'x' || p == 'b' || p == 'o') s.erase(0, 2);
    }

    // digits into a value a little wider than the result, to see overflow
    int wide = width + 8;
    value v(wide, 0);
    bool any = false;
    for (char c : s) {
        if (c == '_') continue;
        int d = std::isdigit((unsigned char)c) ? c - '0' : std::isalpha((unsigned char)c) ? std::tolower((unsigned char)c) - 'a' + 10
                                                                                          : 99;
        if (d >= radix) return std::nullopt;
        // v * radix + d
        value times = radix == 10 ? add(shl(v, 3), shl(v, 1)).v : shl(v, std::countr_zero((unsigned)radix));
        v = add(times, known((uint64_t)d, wide)).v;
        if (!is_zero(v.slice(width, 8))) return std::nullopt;
        any = true;
    }
    if (!any) return std::nullopt;
    auto r = v.slice(0, width);
    if (!negative) return r;
    // the magnitude of a negative number fits the signed range
    if (width > 0 && top_bit(r) && !is_zero(shl(r, 1))) return std::nullopt;
    return negate(r);
}

wave::format wave_format(number_format::kind_t k)
{
    switch (k) {
    case number_format::unsigned_dec: return wave::format::uint;
    case number_format::signed_dec: return wave::format::sint;
    case number_format::binary: return wave::format::binary;
    case number_format::octal: return wave::format::octal;
    default: return wave::format::hex;
    }
}

// ---- ascii ----

std::string ascii_text(const value& v)
{
    std::string s = "\"";
    int bytes = (v.width() + 7) / 8;
    for (int b = bytes - 1; b >= 0; b--) {
        int c = (int)v.slice(8 * b, 8).to_u64() & 0xFF;
        if (c == '"' || c == '\\') {
            s += '\\';
            s += char(c);
        } else if (c >= 32 && c < 127) {
            s += char(c);
        } else {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\x%02X", c);
            s += buf;
        }
    }
    return s + "\"";
}

std::optional<value> parse_ascii(std::string s, int width)
{
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    std::vector<int> chars;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '\\') {
            chars.push_back((unsigned char)s[i]);
            continue;
        }
        if (++i == s.size()) return std::nullopt;
        if (s[i] == 'x') {
            // two hex digits
            int v = 0;
            for (int k = 1; k <= 2; k++) {
                if (i + k >= s.size() || !std::isxdigit((unsigned char)s[i + k])) return std::nullopt;
                v = v * 16 + (std::isdigit((unsigned char)s[i + k]) ? s[i + k] - '0' : std::tolower((unsigned char)s[i + k]) - 'a' + 10);
            }
            chars.push_back(v);
            i += 2;
        } else if (s[i] == '0') {
            chars.push_back(0);
        } else if (s[i] == 'n') {
            chars.push_back('\n');
        } else {
            chars.push_back((unsigned char)s[i]);
        }
    }
    int bytes = (width + 7) / 8;
    if ((int)chars.size() > bytes) return std::nullopt;
    value r(width, 0);
    // the last character is the low byte
    for (size_t k = 0; k < chars.size(); k++)
        r.insert(8 * int(chars.size() - 1 - k), known((uint64_t)chars[k], 8));
    return r;
}

}

std::string format_number(const value& v, number_format f)
{
    if (!v.fully_known() || !model::fits(f.kind, v.width())) return to_string(v);
    int w = v.width();

    switch (f.kind) {
    case number_format::hex:
    case number_format::unsigned_dec:
    case number_format::signed_dec:
    case number_format::binary:
    case number_format::octal: return wave::format_value(v, wave_format(f.kind));

    case number_format::unsigned_fixed:
    case number_format::signed_fixed: {
        bool negative = f.kind == number_format::signed_fixed && top_bit(v);
        uint64_t m = (negative ? negate(v) : v).to_u64();
        // the magnitude of the most negative value is 2^(w-1), it fits
        if (negative && w < 64) m &= mask(w);
        return fixed_text(m, negative, std::clamp(f.frac, 0, 64));
    }

    case number_format::ieee_float:
        if (w == 16) return shortest(half_to_float(uint16_t(v.to_u64())));
        if (w == 32) return shortest(std::bit_cast<float>(uint32_t(v.to_u64())));
        return shortest(std::bit_cast<double>(v.to_u64()));

    case number_format::bfloat16: return shortest(std::bit_cast<float>(uint32_t(v.to_u64()) << 16));

    case number_format::ascii: return ascii_text(v);
    }
    return to_string(v);
}

std::optional<value> parse_number(std::string_view text, int width, number_format f)
{
    auto s = trim(text);
    if (s.empty() || width <= 0 || !model::fits(f.kind, width)) return std::nullopt;

    switch (f.kind) {
    case number_format::hex: return parse_integer(s, width, 16, false);
    case number_format::unsigned_dec: return parse_integer(s, width, 10, false);
    case number_format::signed_dec: return parse_integer(s, width, 10, true);
    case number_format::binary: return parse_integer(s, width, 2, false);
    case number_format::octal: return parse_integer(s, width, 8, false);

    case number_format::unsigned_fixed:
    case number_format::signed_fixed: {
        char* end = nullptr;
        long double x = std::strtold(s.c_str(), &end);
        if (end == s.c_str() || *end != 0 || !std::isfinite(x)) return std::nullopt;
        long double steps = std::nearbyintl(std::ldexpl(x, std::clamp(f.frac, 0, 64)));
        bool is_signed = f.kind == number_format::signed_fixed;
        long double lo = is_signed ? -std::ldexpl(1, width - 1) : 0;
        long double hi = std::ldexpl(1, is_signed ? width - 1 : width) - 1;
        if (steps < lo || steps > hi) return std::nullopt;
        uint64_t bits = steps < 0 ? uint64_t(-(int64_t)(-steps)) : (uint64_t)steps;
        // the most negative 64 bit value doesn't fit int64 as a magnitude
        if (steps < 0 && steps == lo && width == 64) bits = uint64_t(1) << 63;
        return known(bits, width);
    }

    case number_format::ieee_float:
    case number_format::bfloat16: {
        char* end = nullptr;
        double x = std::strtod(s.c_str(), &end);
        if (end == s.c_str() || *end != 0) return std::nullopt;
        if (f.kind == number_format::bfloat16) return known(float_to_bfloat16((float)x), 16);
        if (width == 16) return known(float_to_half((float)x), 16);
        if (width == 32) return known(std::bit_cast<uint32_t>((float)x), 32);
        return known(std::bit_cast<uint64_t>(x), 64);
    }

    case number_format::ascii: return parse_ascii(s, width);
    }
    return std::nullopt;
}

}
