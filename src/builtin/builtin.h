#pragma once

#include "../model/primitive.h"
#include "../sim/component.h"

#include <memory>

namespace logic::builtin {

// a primitive implemented in this program: interface and simulation model
// in one place
class primitive : public model::primitive {
public:
    // the parameters are resolved. null for primitives that are only wiring
    // (see cell_interface::links).
    virtual std::unique_ptr<sim::component> create(const model::param_values& params) const = 0;
};

// all primitives implemented in code
const model::primitive_registry& registry();

// simulation models of the registry's primitives
const sim::component_factory& factory();

// and, or, xor, nand, nor, xnor, not, buffer
void add_gates(model::primitive_registry& r);
// constant, clock
void add_sources(model::primitive_registry& r);
// register
void add_memory(model::primitive_registry& r);
// splitter
void add_wiring(model::primitive_registry& r);

}
