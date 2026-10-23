#include "arith.h"

#include <algorithm>
#include <bit>
#include <vector>

namespace logic::sim {

namespace {

// a gcc and clang extension, for the carries and products of 64 bit words
__extension__ typedef unsigned __int128 u128;

// the known bits of v as words of the given width, extended with zeros
std::vector<uint64_t> words_of(const value& v, int width)
{
    std::vector<uint64_t> r(value::word_count(width), 0);
    for (int w = 0; w < (int)r.size(); w++)
        r[w] = v.bits(w) & ~v.unknown_bits(w);
    if (!r.empty()) r.back() &= word_mask(width, (int)r.size() - 1);
    return r;
}

value from_words(const std::vector<uint64_t>& words, int width)
{
    value r(width, 0);
    for (int w = 0; w < r.words(); w++)
        r.set_word(w, w < (int)words.size() ? words[w] : 0, 0, 0);
    return r;
}

// a = a - b over the words, true if it borrowed out of the top
bool sub_words(std::vector<uint64_t>& a, const std::vector<uint64_t>& b)
{
    uint64_t borrow = 0;
    for (size_t w = 0; w < a.size(); w++) {
        u128 d = (u128)a[w] - (w < b.size() ? b[w] : 0) - borrow;
        a[w] = (uint64_t)d;
        borrow = (d >> 64) ? 1 : 0;
    }
    return borrow != 0;
}

// a >= b, both the same number of words
bool greater_equal(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b)
{
    for (size_t w = a.size(); w-- > 0;)
        if (a[w] != b[w]) return a[w] > b[w];
    return true;
}

value abs_of(const value& a, bool is_signed)
{
    return is_signed && top_bit(a) ? negate(a) : a;
}

}

carry_result add(const value& a, const value& b, bool carry_in)
{
    int width = a.width();
    auto x = words_of(a, width), y = words_of(b, width);
    std::vector<uint64_t> r(x.size());
    u128 carry = carry_in;
    for (size_t w = 0; w < x.size(); w++) {
        u128 s = (u128)x[w] + y[w] + carry;
        r[w] = (uint64_t)s;
        carry = s >> 64;
    }
    // the carry out of the top bit, which may be inside the last word
    bool out = false;
    if (!r.empty()) {
        int top = width - 64 * ((int)r.size() - 1);
        out = top == 64 ? carry != 0 : ((r.back() >> top) & 1) != 0;
    }
    return {from_words(r, width), out};
}

carry_result sub(const value& a, const value& b, bool borrow_in)
{
    // a - b - bin = a + ~b + !bin, no carry out means a borrow
    int width = a.width();
    auto y = words_of(b, width);
    for (auto& w : y)
        w = ~w;
    auto r = add(a, from_words(y, width), !borrow_in);
    return {r.v, !r.carry};
}

value negate(const value& a)
{
    return sub(value(a.width(), 0), a).v;
}

value mul(const value& a, const value& b, bool is_signed)
{
    int width = a.width();
    auto bb = extend(b, width, is_signed ? extend_mode::sign : extend_mode::zero);
    bool negative = is_signed && top_bit(a) != top_bit(bb);
    auto x = words_of(abs_of(a, is_signed), width), y = words_of(abs_of(bb, is_signed), width);

    std::vector<uint64_t> r(value::word_count(2 * width) + 1, 0);
    for (size_t i = 0; i < x.size(); i++) {
        uint64_t carry = 0;
        for (size_t j = 0; j < y.size(); j++) {
            u128 p = (u128)x[i] * y[j] + r[i + j] + carry;
            r[i + j] = (uint64_t)p;
            carry = (uint64_t)(p >> 64);
        }
        for (size_t k = i + y.size(); carry && k < r.size(); k++) {
            u128 s = (u128)r[k] + carry;
            r[k] = (uint64_t)s;
            carry = (uint64_t)(s >> 64);
        }
    }
    auto p = from_words(r, 2 * width);
    return negative ? negate(p) : p;
}

div_result divmod(const value& a, const value& b, bool is_signed)
{
    int width = a.width();
    auto bb = extend(b, width, is_signed ? extend_mode::sign : extend_mode::zero);
    bool neg_q = is_signed && top_bit(a) != top_bit(bb);
    bool neg_r = is_signed && top_bit(a);
    auto n = words_of(abs_of(a, is_signed), width);
    auto d = words_of(abs_of(bb, is_signed), width);

    // shift and subtract, a bit of the dividend at a time from the top
    std::vector<uint64_t> q(n.size(), 0), r(n.size(), 0);
    for (int i = width - 1; i >= 0; i--) {
        // r = r << 1 | bit i of n
        uint64_t in = (n[i / 64] >> (i % 64)) & 1;
        for (size_t w = 0; w < r.size(); w++) {
            uint64_t out = r[w] >> 63;
            r[w] = r[w] << 1 | in;
            in = out;
        }
        if (!r.empty()) r.back() &= word_mask(width, (int)r.size() - 1);
        // the bit shifted out of a full width remainder is larger than d
        if (in || greater_equal(r, d)) {
            sub_words(r, d);
            if (!r.empty()) r.back() &= word_mask(width, (int)r.size() - 1);
            q[i / 64] |= uint64_t(1) << (i % 64);
        }
    }
    auto qv = from_words(q, width), rv = from_words(r, width);
    return {neg_q ? negate(qv) : qv, neg_r ? negate(rv) : rv};
}

value shl(const value& a, int n)
{
    value r(a.width(), 0);
    if (n < a.width()) r.insert(n, a.slice(0, a.width() - n));
    return r;
}

value shr(const value& a, int n)
{
    value r(a.width(), 0);
    if (n < a.width()) r.insert(0, a.slice(n, a.width() - n));
    return r;
}

value sar(const value& a, int n)
{
    if (!top_bit(a)) return shr(a, n);
    n = std::min(n, a.width());
    // ones in the top n bits, the shifted bits below
    auto r = shl(extend(value(0, 0), a.width(), extend_mode::one), a.width() - n);
    if (n < a.width()) r.insert(0, a.slice(n, a.width() - n));
    return r;
}

value rotl(const value& a, int n)
{
    if (a.width() == 0) return a;
    n %= a.width();
    if (n == 0) return a;
    value r(a.width(), 0);
    r.insert(n, a.slice(0, a.width() - n));
    r.insert(0, a.slice(a.width() - n, n));
    return r;
}

value rotr(const value& a, int n)
{
    if (a.width() == 0) return a;
    return rotl(a, a.width() - n % a.width());
}

int compare(const value& a, const value& b, bool is_signed)
{
    int width = a.width();
    auto bb = extend(b, width, is_signed ? extend_mode::sign : extend_mode::zero);
    if (is_signed && top_bit(a) != top_bit(bb)) return top_bit(a) ? -1 : 1;
    // the same sign: the unsigned order is the signed one
    auto x = words_of(a, width), y = words_of(bb, width);
    for (size_t w = x.size(); w-- > 0;)
        if (x[w] != y[w]) return x[w] < y[w] ? -1 : 1;
    return 0;
}

int popcount(const value& a)
{
    int n = 0;
    for (int w = 0; w < a.words(); w++)
        n += std::popcount(a.bits(w) & ~a.unknown_bits(w));
    return n;
}

value extend(const value& a, int width, extend_mode mode)
{
    value r(width, 0);
    bool fill = mode == extend_mode::one || (mode == extend_mode::sign && top_bit(a));
    if (fill && width > a.width()) {
        for (int w = 0; w < r.words(); w++)
            r.set_word(w, ~uint64_t(0), 0, 0);
    }
    r.insert(0, a.slice(0, std::min(a.width(), width)));
    return r;
}

bool is_zero(const value& a)
{
    for (int w = 0; w < a.words(); w++)
        if (a.bits(w) & ~a.unknown_bits(w)) return false;
    return true;
}

bool top_bit(const value& a)
{
    return a.width() > 0 && a.get(a.width() - 1) == bit_state::one;
}

}
