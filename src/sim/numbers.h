#pragma once

#include "../model/number_format.h"
#include "value.h"

#include <optional>
#include <string>
#include <string_view>

namespace logic::sim {

// v in the format. values with unknown bits (and formats that don't fit the
// width) show the bits as sim::to_string does.
std::string format_number(const value& v, model::number_format f);

// text in the format as a value of the width, null if it isn't one or
// doesn't fit. integer formats also take a 0x, 0b or 0o prefix for another
// radix and '_' between digits; fixed point rounds to the nearest step.
std::optional<value> parse_number(std::string_view text, int width, model::number_format f);

}
