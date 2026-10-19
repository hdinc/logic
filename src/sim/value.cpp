#include "value.h"

#include <algorithm>
#include <cctype>

namespace logic::sim {

uint64_t mask(int width)
{
    return width >= 64 ? ~uint64_t(0) : width <= 0 ? 0 : (uint64_t(1) << width) - 1;
}

uint64_t word_mask(int width, int w)
{
    return mask(width - 64 * w);
}

value::value(int width, uint64_t bits, uint64_t x, uint64_t z)
    : m_width(std::max(width, 0))
{
    if (m_width > 64) m_big.assign(3 * size_t(words()), 0);
    if (m_width > 0) set_word(0, bits, x, z);
}

void value::set_word(int w, uint64_t bits, uint64_t x, uint64_t z)
{
    if (w < 0 || w >= words()) return;
    auto m = word_mask(m_width, w);
    x &= m;
    z &= m & ~x;
    bits &= m & ~x & ~z;
    auto d = data();
    int n = words();
    d[w] = bits;
    d[n + w] = x;
    d[2 * n + w] = z;
}

bit_state value::get(int i) const
{
    if (i < 0 || i >= m_width) return bit_state::z;
    uint64_t b = uint64_t(1) << (i % 64);
    int w = i / 64;
    if (x(w) & b) return bit_state::x;
    if (z(w) & b) return bit_state::z;
    return (bits(w) & b) ? bit_state::one : bit_state::zero;
}

void value::set(int i, bit_state s)
{
    if (i < 0 || i >= m_width) return;
    uint64_t b = uint64_t(1) << (i % 64);
    int w = i / 64;
    uint64_t bt = bits(w) & ~b, xt = x(w) & ~b, zt = z(w) & ~b;
    switch (s) {
    case bit_state::zero: break;
    case bit_state::one: bt |= b; break;
    case bit_state::x: xt |= b; break;
    case bit_state::z: zt |= b; break;
    }
    set_word(w, bt, xt, zt);
}

bool value::fully_known() const
{
    for (int w = 0; w < words(); w++)
        if (unknown_bits(w)) return false;
    return true;
}

bool value::fully_floating() const
{
    for (int w = 0; w < words(); w++)
        if (z(w) != word_mask(m_width, w)) return false;
    return true;
}

namespace {

// 64 bits of the masks starting at bit lo, bits beyond the width are z
void read64(const value& v, int lo, uint64_t& b, uint64_t& x, uint64_t& z)
{
    auto part = [&](int w, int shift, uint64_t& rb, uint64_t& rx, uint64_t& rz) {
        // word w shifted right by `shift`, or left by -shift
        if (w < 0 || w >= v.words()) {
            rb = rx = 0;
            rz = ~uint64_t(0);
            // only the part that lands in the result
            if (shift > 0) rz >>= shift;
            else if (shift < 0) rz <<= -shift;
            return;
        }
        auto m = word_mask(v.width(), w);
        uint64_t wb = v.bits(w), wx = v.x(w), wz = v.z(w) | ~m;
        if (shift >= 0) {
            rb = wb >> shift;
            rx = wx >> shift;
            rz = wz >> shift;
        } else {
            rb = wb << -shift;
            rx = wx << -shift;
            rz = wz << -shift;
        }
    };

    int w = lo >= 0 ? lo / 64 : (lo - 63) / 64;
    int s = lo - 64 * w;
    uint64_t b0, x0, z0;
    part(w, s, b0, x0, z0);
    if (s == 0) {
        b = b0;
        x = x0;
        z = z0;
        return;
    }
    uint64_t b1, x1, z1;
    part(w + 1, s - 64, b1, x1, z1);
    b = b0 | b1;
    x = x0 | x1;
    z = z0 | z1;
}

}

value value::slice(int lo, int width) const
{
    value r(width, 0);
    for (int w = 0; w < r.words(); w++) {
        uint64_t b, x, z;
        read64(*this, lo + 64 * w, b, x, z);
        r.set_word(w, b, x, z);
    }
    return r;
}

void value::insert(int lo, const value& v)
{
    if (lo == 0 && v.m_width == m_width) {
        *this = v;
        return;
    }
    for (int w = 0; w < words(); w++) {
        int start = 64 * w;
        // bits of this word that v covers
        int from = std::max(start, lo), to = std::min(start + 64, std::min(lo + v.width(), m_width));
        if (from >= to) continue;
        uint64_t m = mask(to - from) << (from - start);
        uint64_t b, x, z;
        read64(v, start - lo, b, x, z);
        set_word(w, (bits(w) & ~m) | (b & m), (this->x(w) & ~m) | (x & m), (this->z(w) & ~m) | (z & m));
    }
}

bool value::operator==(const value& o) const
{
    if (m_width != o.m_width) return false;
    auto a = data(), b = o.data();
    return std::equal(a, a + 3 * words(), b);
}

value known(uint64_t v, int width)
{
    return {width, v};
}

value unknown(int width)
{
    value r(width, 0);
    for (int w = 0; w < r.words(); w++)
        r.set_word(w, 0, ~uint64_t(0), 0);
    return r;
}

value floating(int width)
{
    value r(width, 0);
    for (int w = 0; w < r.words(); w++)
        r.set_word(w, 0, 0, ~uint64_t(0));
    return r;
}

value resolve(std::span<const value> drivers, int width)
{
    value r(width, 0);
    for (int w = 0; w < r.words(); w++) {
        auto m = word_mask(width, w);
        uint64_t one = 0, zero = 0, x = 0;
        for (auto& d : drivers) {
            // bits beyond a driver's width are not driven by it
            auto driven = ~d.z(w) & word_mask(d.width(), w) & m;
            one |= d.bits(w) & driven;
            zero |= ~d.bits(w) & ~d.x(w) & driven;
            x |= d.x(w) & driven;
        }
        x |= one & zero;
        r.set_word(w, one & ~x, x, ~(one | zero | x) & m);
    }
    return r;
}

namespace {

char bit_char(bit_state b)
{
    switch (b) {
    case bit_state::zero: return '0';
    case bit_state::one: return '1';
    case bit_state::x: return 'x';
    case bit_state::z: return 'z';
    }
    return '?';
}

}

std::string to_hex(const value& v)
{
    std::string s;
    int digits = std::max(1, (v.width() + 3) / 4);
    for (int d = digits - 1; d >= 0; d--) {
        int w = d / 16, shift = 4 * (d % 16);
        auto m = (uint64_t(0xF) << shift) & word_mask(v.width(), w);
        if (v.z(w) & m && (v.z(w) & m) == m) s += 'z';
        else if (v.unknown_bits(w) & m) s += 'x';
        else s += "0123456789ABCDEF"[(v.bits(w) >> shift) & 0xF];
    }
    return s;
}

std::string to_string(const value& v)
{
    if (v.width() == 1) return std::string(1, bit_char(v.get(0)));
    if (v.fully_known()) return "0x" + to_hex(v);
    // one character per bit gets too long for wide values
    if (v.width() > 64) return "0x" + to_hex(v);

    std::string s;
    for (int i = v.width() - 1; i >= 0; i--)
        s += bit_char(v.get(i));
    return s;
}

std::optional<value> parse_hex(std::string_view s, int width)
{
    while (!s.empty() && std::isspace((unsigned char)s.front()))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace((unsigned char)s.back()))
        s.remove_suffix(1);
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s.remove_prefix(2);

    value r = known(0, width);
    int bit = 0;
    bool any = false;
    for (auto it = s.rbegin(); it != s.rend(); ++it) {
        char c = *it;
        if (c == '_') continue;
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return std::nullopt;
        any = true;
        for (int k = 0; k < 4; k++, bit++) {
            bool one = d >> k & 1;
            if (bit >= width) {
                if (one) return std::nullopt;
                continue;
            }
            if (one) r.set(bit, bit_state::one);
        }
    }
    if (!any) return std::nullopt;
    return r;
}

}
