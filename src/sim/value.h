#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace logic::sim {

// state of one bit
enum class bit_state : uint8_t {
    zero,
    one,
    x, // unknown, e.g. a conflict
    z  // not driven
};

// a value of any width, each bit 0, 1, x or z. stored as three masks of 64
// bit words: bits, x and z. a bit is set in at most one of x and z, bits is
// 1 only for known 1s, bits above the width are 0 in all masks. values up
// to 64 bits don't allocate.
class value {
public:
    value() = default;
    // word 0 of the masks, the others are 0. the masks are cleaned up to keep
    // the rules above.
    value(int width, uint64_t bits, uint64_t x = 0, uint64_t z = 0);

    int width() const { return m_width; }
    int words() const { return word_count(m_width); }
    static int word_count(int width) { return (width + 63) / 64; }

    // word w of the masks, 0 beyond the width
    uint64_t bits(int w = 0) const { return w < words() ? data()[w] : 0; }
    uint64_t x(int w = 0) const { return w < words() ? data()[words() + w] : 0; }
    uint64_t z(int w = 0) const { return w < words() ? data()[2 * words() + w] : 0; }
    // known bits, x and z of word w together
    uint64_t unknown_bits(int w = 0) const { return x(w) | z(w); }

    // sets word w, cleaned up like in the constructor
    void set_word(int w, uint64_t bits, uint64_t x, uint64_t z);

    bit_state get(int i) const;
    void set(int i, bit_state b);

    bool fully_known() const;
    // every bit is z, true for width 0
    bool fully_floating() const;
    // the low 64 bits as a number, x and z bits count as 0
    uint64_t to_u64() const { return bits(0); }

    // bits [lo, lo + width), bits beyond this value are z
    value slice(int lo, int width) const;
    // replaces bits [lo, lo + v.width()) with v, the part beyond the width
    // is dropped
    void insert(int lo, const value& v);

    bool operator==(const value& o) const;

private:
    uint64_t* data() { return m_width <= 64 ? m_small : m_big.data(); }
    const uint64_t* data() const { return m_width <= 64 ? m_small : m_big.data(); }

    int m_width = 0;
    // bits, x, z for up to 64 bits
    uint64_t m_small[3] = {};
    // all bits words, then all x words, then all z words
    std::vector<uint64_t> m_big;
};

// mask of the valid bits in word w of a value of the given width
uint64_t word_mask(int width, int w = 0);
// low `width` bits set, all for 64 and more
uint64_t mask(int width);

// v in the low 64 bits, the rest 0
value known(uint64_t v, int width);
value unknown(int width);
value floating(int width);

// the value of a net with several drivers: undriven bits are z, bits driven
// to different values or to x are x
value resolve(std::span<const value> drivers, int width);

// "0", "1", "x", "z" for one bit. wider values in hex if fully known,
// otherwise one character per bit, most significant first
std::string to_string(const value& v);
// hex without prefix, digits of unknown bits as x
std::string to_hex(const value& v);
// hex digits with an optional 0x and '_' separators. null if it isn't hex or
// doesn't fit the width.
std::optional<value> parse_hex(std::string_view s, int width);

}
