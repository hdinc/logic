#include "value.h"

#include <cstdio>

namespace logic::sim {

uint64_t mask(int width)
{
    return width >= 64 ? ~uint64_t(0) : (uint64_t(1) << width) - 1;
}

bool value::fully_known(int width) const
{
    return ((x | z) & mask(width)) == 0;
}

value known(uint64_t v, int width)
{
    return {v & mask(width), 0, 0};
}

value unknown(int width)
{
    return {0, mask(width), 0};
}

value floating(int width)
{
    return {0, 0, mask(width)};
}

value resolve(std::span<const value> drivers, int width)
{
    auto m = mask(width);
    uint64_t one = 0, zero = 0, x = 0;
    for (auto& d : drivers) {
        auto driven = ~d.z & m;
        one |= d.bits & driven;
        zero |= ~d.bits & ~d.x & driven;
        x |= d.x & driven;
    }
    x |= one & zero;
    return {one & ~x, x, ~(one | zero | x) & m};
}

std::string to_string(const value& v, int width)
{
    auto bit = [&](int i) {
        uint64_t b = uint64_t(1) << i;
        if (v.x & b) return 'x';
        if (v.z & b) return 'z';
        return (v.bits & b) ? '1' : '0';
    };

    if (width == 1) return std::string(1, bit(0));

    if (v.fully_known(width)) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)(v.bits & mask(width)));
        return buf;
    }

    std::string s;
    for (int i = width - 1; i >= 0; i--)
        s += bit(i);
    return s;
}

}
