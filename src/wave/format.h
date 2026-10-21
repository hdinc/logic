#pragma once

#include "trace.h"

#include <optional>
#include <string>
#include <string_view>

namespace logic::wave {

enum class format { hex, uint, sint, binary, octal };
constexpr const char* format_names[] = {"Hex", "UInt", "Int", "Binary", "Octal"};
// "hex", "uint", ... for session files
const char* format_id(format f);
std::optional<format> parse_format_id(std::string_view s);

// e.g. "0xFF", "255", "-1", "0b11111111", "0o377". values that aren't fully
// known show their bits in decimal formats.
std::string format_value(const sim::value& v, format f);
// change i of t in format f, reals with %g and texts as they are
std::string format_change(const trace& t, size_t i, format f);
// a real number with %g
std::string format_real(double v);

// what to search a signal for. digits are of the format's radix, a 0x, 0b
// or 0o prefix picks another one; decimal for the integer formats. x and z
// digits must match, ? matches anything.
struct pattern {
    kind type = kind::digital;
    sim::value v;
    // bits that must match, words like sim::value's bits
    std::vector<uint64_t> care;
    double real = 0;
    std::string text;

    bool matches(const trace& t, size_t i) const;
};
// nullopt if text isn't a value for a signal of that kind and width
std::optional<pattern> parse_pattern(std::string_view text, kind k, int width, format f);

}
