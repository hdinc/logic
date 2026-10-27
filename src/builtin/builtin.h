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
    // the same with the folder relative files start at, for primitives that
    // load files. an error if it can't be created.
    virtual std::unique_ptr<sim::component> create(const sim::create_info& info, std::string& error) const
    {
        (void)error;
        return create(info.params);
    }

    model::symbol icon() const override;
};

// the component bar's icon of a builtin kind, see model::primitive::icon.
// empty for kinds without one.
model::symbol icon(const std::string& kind);

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
// the segments a (bit 0) to g (bit 6) lit for a hex digit, 0 to 15
uint8_t seg7_hex(int digit);
// segments a to g and the point of a 7-segment display: 1 lit, 0 dark or
// floating, -1 unknown. from its resolved parameters, its ports and their
// values (none, e.g. while editing: all dark).
void seg7_levels(const model::param_values& params, const std::vector<model::port>& ports, std::span<const sim::value> values, int out[8]);

// led, displays, button, switch, terminal, framebuffer
void add_io(model::primitive_registry& r);
// adder/subtractor, multiplier, divider, negator, comparator, shifter, bit
// extender, counter
void add_arith(model::primitive_registry& r);
// mux, demux, decoder, priority encoder, bit counter, bit selector
void add_plexers(model::primitive_registry& r);

}
