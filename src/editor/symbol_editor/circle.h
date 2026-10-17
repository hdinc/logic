#pragma once

#include "control_point.h"

namespace logic::editor::symbol_editor {

class circle {
public:
    circle(control_point* p1, control_point* p2);
    void draw(glm::mat4& vp, Hd::Graphics2D& g);
    // non owning pointer, assumed always valid
    control_point* p1;
    control_point* p2;
};

}