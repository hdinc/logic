#pragma once

#include "../model/number_format.h"
#include "../sim/value.h"

namespace logic::editor {

// a combo of the formats that fit the width, and the fraction bits for
// fixed point. returns true if f changed.
bool edit_number_format(const char* label, model::number_format& f, int width);

// the value as text in the format, enter sets it. text that isn't a value
// in the format is shown red and not applied. returns true if v changed.
bool edit_number(const char* label, sim::value& v, int width, model::number_format f);

// a button per bit, top bit first, in groups of 4. a click flips a known
// bit, an unknown one becomes 1. returns true if v changed.
bool edit_bits(sim::value& v);

}
