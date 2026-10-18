#pragma once

#include "cell.h"
#include "param.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace logic::model {

// what an instance looks like from outside: its ports and symbol
struct cell_interface {
    std::vector<port> ports;
    model::symbol symbol;
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

    virtual std::vector<param_def> params() const { return {}; }

    // values are resolved: every parameter has a valid value
    virtual cell_interface build(const param_values& values) const = 0;
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
