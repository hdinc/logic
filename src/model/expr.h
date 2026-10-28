#pragma once

#include "param.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace logic::model {

// integer expressions for widths and parameter values, e.g. "DATA_W / 8":
// decimal numbers, names of integer parameters in scope, + - * / %, unary -,
// parentheses and the functions clog2(x), min(a, b) and max(a, b). division
// rounds toward zero. nullopt with a reason on an error: an unknown name,
// division by zero, overflow or bad syntax.
std::optional<int64_t> eval_expr(const std::string& text, const param_values& scope, std::string& error);

// a plain decimal number, optionally negative, with spaces around it
bool is_literal(const std::string& text);

// the names an expression uses, in order of appearance, without repeats
// and without the function names
std::vector<std::string> expr_names(const std::string& text);

// a name usable for a parameter: a letter or '_' followed by letters,
// digits and '_', and not a function name
bool valid_param_name(const std::string& name);

}
