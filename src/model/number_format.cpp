#include "number_format.h"

namespace logic::model {

namespace {

struct kind_names {
    const char* label;
    const char* id;
};

constexpr kind_names names[number_kind_count] = {
    {"Hex", "hex"},
    {"Unsigned", "unsigned"},
    {"Signed", "signed"},
    {"Binary", "binary"},
    {"Octal", "octal"},
    {"Unsigned fixed", "ufixed"},
    {"Signed fixed", "sfixed"},
    {"Float", "float"},
    {"bfloat16", "bfloat16"},
    {"ASCII", "ascii"},
};

}

const char* kind_label(number_format::kind_t k)
{
    return names[k].label;
}

const char* kind_id(number_format::kind_t k)
{
    return names[k].id;
}

std::optional<number_format::kind_t> parse_number_kind(std::string_view s)
{
    for (int k = 0; k < number_kind_count; k++)
        if (s == names[k].id) return (number_format::kind_t)k;
    return std::nullopt;
}

bool fits(number_format::kind_t k, int width)
{
    switch (k) {
    case number_format::ieee_float: return width == 16 || width == 32 || width == 64;
    case number_format::bfloat16: return width == 16;
    case number_format::unsigned_fixed:
    case number_format::signed_fixed: return width <= 64;
    default: return true;
    }
}

}
