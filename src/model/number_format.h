#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace logic::model {

// how a value is shown and entered, e.g. at a port while simulating
struct number_format {
    enum kind_t {
        hex,
        unsigned_dec,
        signed_dec,
        binary,
        octal,
        // a number with frac fraction bits, value / 2^frac
        unsigned_fixed,
        signed_fixed,
        // ieee 754 half, single or double by the width (16, 32, 64 bits)
        ieee_float,
        // the top 16 bits of a single, 16 bits
        bfloat16,
        // bytes as characters, the first character is the top byte
        ascii,
    };
    kind_t kind = hex;
    // fixed point only
    int frac = 0;

    bool operator==(const number_format&) const = default;
};

constexpr int number_kind_count = 10;

// "Hex", "Signed fixed", ... for the user
const char* kind_label(number_format::kind_t k);
// "hex", "sfixed", ... for the files
const char* kind_id(number_format::kind_t k);
std::optional<number_format::kind_t> parse_number_kind(std::string_view id);

// the format can show values of this width: floats need their widths,
// fixed point and floats at most 64 bits
bool fits(number_format::kind_t k, int width);

}
