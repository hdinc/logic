#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace logic::model {

// ids of elements inside a cell (ports, wires, instances, shapes, pins) are
// unique within that cell. cell ids are unique within the workspace.
using id = uint64_t;
constexpr id no_id = 0;

// schematic elements (ports, wires, instances, pins) are placed on this grid
constexpr int grid = 10;

enum class direction {
    input,
    output,
    inout
};

// rotation in quarter turns counter clockwise, applied after mirroring along x
struct transform {
    int rotation = 0;
    bool mirror = false;

    glm::ivec2 apply(glm::ivec2 p) const;
    glm::vec2 apply(glm::vec2 p) const;
    bool operator==(const transform&) const = default;
};

struct rect {
    glm::vec2 min{0, 0};
    glm::vec2 max{0, 0};

    bool contains(glm::vec2 p) const;
    void expand(glm::vec2 p);
    void expand(rect r);
};

// rect spanned by two arbitrary corners
rect make_rect(glm::vec2 a, glm::vec2 b);

inline glm::ivec2 snap(glm::vec2 p, int step)
{
    return glm::ivec2(glm::round(p / float(step))) * step;
}

}
