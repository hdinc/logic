#pragma once

#include "param.h"
#include "types.h"

#include <string>
#include <vector>

namespace logic::model {

struct wire {
    model::id id = no_id;
    glm::ivec2 a;
    glm::ivec2 b;
    bool operator==(const wire&) const = default;
};

// placement of a cell inside a schematic
struct instance {
    model::id id = no_id;
    model::id cell = no_id;
    std::string name;
    glm::ivec2 pos{0, 0};
    transform xf;
    // configuration of primitives, only values that differ from the defaults
    param_values params;
    bool operator==(const instance&) const = default;
};

struct schematic {
    std::vector<wire> wires;
    std::vector<instance> instances;

    wire* find_wire(id i);
    instance* find_instance(id i);
    bool operator==(const schematic&) const = default;
};

// a name like the editor gives new instances: U and digits, e.g. U12
bool is_default_instance_name(const std::string& name);

// merges overlapping collinear wires and splits wires at points where other
// wires end or at the given extra points (pins), so every junction is a wire
// endpoint. new wires get fresh ids from next_id.
std::vector<wire> normalize_wires(const std::vector<wire>& wires, const std::vector<glm::ivec2>& extra_points, id& next_id);

}
