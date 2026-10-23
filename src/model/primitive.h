#pragma once

#include "cell.h"
#include "param.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace logic::model {

class workspace;

// bits of two ports wired together inside a primitive, e.g. by a splitter.
// the simulator joins them like a wire, without delay and in both directions.
struct bit_link {
    // port indices and the first bit of each
    int a = 0;
    int a_bit = 0;
    int b = 0;
    int b_bit = 0;
    int width = 1;
};

// a weak 0 or 1 on the net of a port, e.g. by a pull resistor. bits nothing
// else drives get the pulled value instead of z.
struct pull {
    int port = 0;
    bool up = false;
};

// a port of the simulation model that is bits [bit, bit + port.width) of
// port `of` of the interface, e.g. a field of a bundle port
struct model_port {
    model::port port;
    int of = 0;
    int bit = 0;
};

// what an instance looks like from outside: its ports and symbol
struct cell_interface {
    std::vector<port> ports;
    model::symbol symbol;
    std::vector<bit_link> links;
    std::vector<pull> pulls;
    // the ports the simulation model sees when they differ from ports (see
    // port_group), in the model's order. empty if they are the ports.
    std::vector<model_port> model_ports;
    // a tunnel: its pin is connected to the pins of all tunnels with the
    // same name in the schematic. empty for other parts.
    std::string tunnel;
};

// what a primitive may look up while building its interface besides its
// parameters. the workspace is null when there is none, e.g. for the cells
// of the builtin library.
struct build_context {
    const workspace* ws = nullptr;
};

// a cell implemented in code, e.g. a gate or a register. its ports and symbol
// are built from the parameters of each instance. the simulation behaviour is
// added by the simulator, the model only knows the interface.
class primitive {
public:
    virtual ~primitive() = default;

    // identifies the primitive in builtin_impl and the files, never changes
    virtual std::string kind() const = 0;
    // name of the cell in the builtin library
    virtual std::string name() const = 0;
    virtual std::string description() const { return {}; }
    // group in the component bar, e.g. "Logic gates"
    virtual std::string category() const { return {}; }

    virtual std::vector<param_def> params() const { return {}; }

    // values are resolved: every parameter has a valid value
    virtual cell_interface build(const param_values& values) const = 0;

    // primitives whose interface depends on bundle types override this and
    // uses_bundles. the interface is rebuilt when a bundle type changes.
    virtual cell_interface build(const param_values& values, const build_context&) const { return build(values); }
    virtual bool uses_bundles() const { return false; }

    // only connects nets (splitters, tunnels, ...), it has no simulation
    // model
    virtual bool is_wiring() const { return false; }

    // a small drawing for the component bar in a 24 by 24 box around the
    // origin, the body of the part without pins. empty: its symbol is drawn.
    virtual symbol icon() const { return {}; }

    // problem with a combination of resolved values that build() works
    // around, shown to the user. empty if there is none.
    virtual std::string check(const param_values&) const { return {}; }
    virtual std::string check(const param_values& values, const build_context&) const { return check(values); }
};

class primitive_registry {
public:
    void add(std::unique_ptr<primitive> p);
    const primitive* find(const std::string& kind) const;

    // in the order they were added
    const std::vector<std::unique_ptr<primitive>>& all() const { return list; }

private:
    std::vector<std::unique_ptr<primitive>> list;
};

// cell id of a builtin, the same on every run so saved schematics can refer
// to builtin cells by id like to any other cell
id builtin_cell_id(const std::string& kind);

}
