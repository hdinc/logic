#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace logic::sim {

// up to 64 bits, each 0, 1, x (unknown, e.g. a conflict) or z (not driven).
// a bit is set in at most one of the masks, bits is 1 only for known 1s.
struct value {
    uint64_t bits = 0;
    uint64_t x = 0;
    uint64_t z = 0;

    bool operator==(const value&) const = default;

    uint64_t known() const { return ~(x | z); }
    bool fully_known(int width) const;
};

uint64_t mask(int width);

value known(uint64_t v, int width);
value unknown(int width);
value floating(int width);

// the value of a net with several drivers: undriven bits are z, bits driven
// to different values or to x are x
value resolve(std::span<const value> drivers, int width);

// "0", "1", "x", "z" for one bit. wider values in hex if fully known,
// otherwise one character per bit, most significant first
std::string to_string(const value& v, int width);

}
