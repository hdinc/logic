#pragma once

#include "types.h"

#include <string>
#include <variant>
#include <vector>

namespace logic::model {

struct line {
    glm::ivec2 a;
    glm::ivec2 b;
};

// quadratic bezier from a to b with control point c
struct bezier {
    glm::ivec2 a;
    glm::ivec2 b;
    glm::ivec2 c;
};

struct circle {
    glm::ivec2 center;
    float radius;
};

struct text {
    glm::ivec2 pos;
    std::string str = "text";
    float size = 10.0f;
};

struct shape {
    model::id id = no_id;
    std::variant<line, bezier, circle, text> geom;
};

// editable points of a shape. circles have their center and a radius handle at
// center + (radius, 0)
int point_count(const shape& s);
glm::ivec2 get_point(const shape& s, int i);
void set_point(shape& s, int i, glm::ivec2 p);

// connection point of the symbol, linked to one of the cell's ports
struct pin {
    model::id id = no_id;
    model::id port = no_id;
    glm::ivec2 pos;
};

struct symbol {
    std::vector<shape> shapes;
    std::vector<pin> pins;

    shape* find_shape(id i);
    pin* find_pin(id i);
    const pin* find_pin_of_port(id port) const;

    // bounding box of all shapes and pins, used for hit testing instances
    rect bounds() const;
};

}
