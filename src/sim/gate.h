#pragma once

#include "net.h"
#include <glm/glm.hpp>
#include "../draw/draw.h"

class gate {
public:

    gate() = default;

    gate(const gate&);

    gate& operator=(const gate&) = delete;

    bool need_update = 1;

    virtual void update() = 0;

    virtual ~gate() = 0;

    virtual void draw(drawer& d) = 0;

    std::vector<pin> pins;
    std::vector<glm::vec2> pin_positions;

    glm::ivec2 box {1, 1};

    bool selected = false;

    glm::ivec2 pos {0, 0};
};
