#pragma once

#include "schematic.h"
#include "symbol.h"

#include <string>
#include <variant>
#include <vector>

namespace logic::model {

struct port {
    model::id id = no_id;
    std::string name;
    direction dir = direction::input;
    int width = 1;

    // position of the port's connector inside the schematic. unused for cells
    // that are not implemented by a schematic.
    glm::ivec2 pos{0, 0};
    bool operator==(const port&) const = default;
};

// where wires connect to the port's connector in the schematic
glm::ivec2 connection_point(const port& p);

// cell implemented in c++ by the simulator, identified by kind
struct builtin_impl {
    std::string kind;
    bool operator==(const builtin_impl&) const = default;
};

using implementation = std::variant<schematic, builtin_impl>;

// anything that can be placed in a schematic: user circuits, builtin
// primitives and (later) c and verilator models
struct cell {
    model::id id = no_id;
    std::string name;

    std::vector<port> ports;
    model::symbol symbol;
    implementation impl;

    // next free id for elements inside this cell
    model::id next_id = 1;
    model::id new_id() { return next_id++; }

    port* find_port(model::id i);
    const port* find_port(model::id i) const;

    schematic* get_schematic() { return std::get_if<schematic>(&impl); }
    const schematic* get_schematic() const { return std::get_if<schematic>(&impl); }

    // the returned reference is invalidated by the next add_port
    port& add_port(direction dir, glm::ivec2 pos);
    // removes the port and the symbol pins linked to it
    void remove_port(model::id i);

    std::string unique_port_name(const std::string& prefix) const;

    // replaces the symbol with a box, inputs on the left, outputs and inouts
    // on the right, ordered by their position in the schematic
    void generate_symbol();

    // human readable problems that prevent using the cell, e.g. ports without
    // a pin on the symbol
    std::vector<std::string> check() const;
};

const char* to_string(direction d);

// world position of a symbol pin of the instance
glm::ivec2 pin_position(const instance& i, const pin& p);

// bounding box of the placed instance. c may be null if the cell is missing
rect instance_bounds(const instance& i, const cell* c);

}
