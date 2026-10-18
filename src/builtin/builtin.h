#pragma once

#include "../model/primitive.h"

namespace logic::builtin {

// all primitives implemented in code
const model::primitive_registry& registry();

// and, or, xor, nand, nor, xnor, not, buffer
void add_gates(model::primitive_registry& r);
// constant, clock
void add_sources(model::primitive_registry& r);

}
