#pragma once

#include "bundle.h"
#include "param.h"
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
    // shown when hovering the pin of an instance
    std::string description;

    // a port of a bundle type: width is the bundle's total width (kept up to
    // date by the workspace) and the fields have their own directions. dir
    // only says on which side the port sits. flipped is the other view, its
    // inputs and outputs are swapped.
    model::id bundle = no_id;
    bool flipped = false;

    // position of the port's connector inside the schematic. unused for cells
    // that are not implemented by a schematic.
    glm::ivec2 pos{0, 0};
    bool operator==(const port&) const = default;
};

// where wires connect to the port's connector in the schematic
glm::ivec2 connection_point(const port& p);

// a bundle port of a c model or hdl cell made of the model's own ports: the
// ports named prefix + the field's path, with '_' for '.', are the fields,
// e.g. m0_aw_valid for field aw.valid with prefix "m0_". the model sees its
// ports as they are.
struct port_group {
    // of the bundle port
    std::string name;
    model::id bundle = no_id;
    bool flipped = false;
    // empty for name + "_"
    std::string prefix;
    // the side of the generated symbol: inputs left, the others right
    direction side = direction::output;
    std::string description;

    std::string field_prefix() const { return prefix.empty() ? name + "_" : prefix; }
    bool operator==(const port_group&) const = default;
};

// the model's port for a field of a port group
std::string field_port_name(const port_group& g, const std::string& path);

// ports whose names share a prefix up to their last '_', e.g. io_jtag_tms
// and io_jtag_tck share "io_jtag_". only prefixes of two or more ports, in
// the order of their first port.
struct port_prefix {
    std::string prefix;
    // indices of the ports, the rest of their names are the fields
    std::vector<int> ports;
};
std::vector<port_prefix> common_prefixes(const std::vector<port>& ports);

// cell implemented in c++ by the simulator, identified by kind
struct builtin_impl {
    std::string kind;
    bool operator==(const builtin_impl&) const = default;
};

enum class hdl_language {
    verilog,
    systemverilog,
    vhdl
};

enum class hdl_backend {
    verilator,
    icarus,
    ghdl
};

// cell implemented by a model in a plugin (a shared library with the c
// model abi). the ports, symbol and parameters are stored in the cell, so it
// can be placed without the plugin.
struct c_model_impl {
    // plugin file name without extension, or a path relative to the library
    // folder
    std::string plugin;
    // name of the model inside the plugin
    std::string model;
    // declared by the model, found when the interface is refreshed
    std::vector<param_def> params;
    // bundle ports made of the model's ports. the cell's ports are then the
    // grouped ones, leaf_ports the model's.
    std::vector<port_group> groups;
    std::vector<port> leaf_ports;
    bool operator==(const c_model_impl&) const = default;
};

// cell implemented in verilog, systemverilog or vhdl and simulated by an
// external tool. ports, symbol and parameters are stored like for c models.
struct hdl_impl {
    hdl_language language = hdl_language::verilog;
    hdl_backend backend = hdl_backend::verilator;
    // relative to the library folder
    std::vector<std::string> sources;
    // top module or entity
    std::string top;
    std::vector<std::string> include_dirs;
    // NAME or NAME=VALUE
    std::vector<std::string> defines;
    // extra arguments for the tool
    std::string args;
    // parameters (verilog) or generics (vhdl) of the top
    std::vector<param_def> params;
    // see c_model_impl
    std::vector<port_group> groups;
    std::vector<port> leaf_ports;
    bool operator==(const hdl_impl&) const = default;
};

// a cell from a newer version of the program, kept as it was so saving
// doesn't lose it. it can be placed but not simulated.
struct unknown_impl {
    std::string type;
    // the whole implementation as json text
    std::string json;
    bool operator==(const unknown_impl&) const = default;
};

using implementation = std::variant<schematic, builtin_impl, c_model_impl, hdl_impl, unknown_impl>;

const char* to_string(hdl_language l);
const char* to_string(hdl_backend b);
// false if the backend can't simulate the language
bool supports(hdl_backend b, hdl_language l);

// how generate_symbol orders the pins of a side
enum class pin_order {
    // by the position of the ports in the schematic, top first
    position,
    // in the order of the ports, e.g. as declared in hdl
    declaration
};

// anything that can be placed in a schematic: user circuits, builtin
// primitives, c models and hdl models
struct cell {
    model::id id = no_id;
    std::string name;
    // groups cells in the library panel and the component bar, empty for
    // none. like the name it's not part of the undo history.
    std::string category;

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

    // implemented outside the program: a c model or hdl
    bool is_external() const { return std::holds_alternative<c_model_impl>(impl) || std::holds_alternative<hdl_impl>(impl); }
    // parameters of a c model or hdl cell, null for other cells
    const std::vector<param_def>* external_params() const;
    std::vector<param_def>* external_params();
    // bundle ports made of the model's ports, null for cells that are not
    // external
    const std::vector<port_group>* port_groups() const;
    std::vector<port_group>* port_groups();
    // the ports as the simulation model has them: the model's own ports of
    // an external cell with port groups, the cell's ports otherwise
    const std::vector<port>& model_ports() const;
    std::vector<port>* leaf_ports();

    // the returned reference is invalidated by the next add_port
    port& add_port(direction dir, glm::ivec2 pos);
    // removes the port and the symbol pins linked to it
    void remove_port(model::id i);

    std::string unique_port_name(const std::string& prefix) const;

    // replaces the symbol with a box, inputs on the left, outputs and inouts
    // on the right. buses get thick stubs, bundles thicker ones.
    void generate_symbol(pin_order order = pin_order::position);

    // replaces the ports with the given ones, e.g. found in a model. ports
    // with the same name keep their id, position and description (unless the
    // new one has a description). the symbol is kept if the names and
    // directions stay the same, otherwise generated. returns true if it was
    // generated.
    bool set_ports(const std::vector<port>& ports);

    // human readable problems that prevent using the cell, e.g. ports without
    // a pin on the symbol
    std::vector<std::string> check() const;
};

const char* to_string(direction d);

// world position of a symbol pin of the instance
glm::ivec2 pin_position(const instance& i, const pin& p);

// bounding box of the placed instance. s is null if the cell is missing
rect instance_bounds(const instance& i, const symbol* s);

}
