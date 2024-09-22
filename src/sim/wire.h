#pragma once

#include <glm/glm.hpp>
#include "../draw/draw.h"

class wire {
public:
    wire(glm::ivec2 p1_, glm::ivec2 p2_)
        : p1{p1_}
        , p2{p2_}
    {
    }

    // wire(const wire&) = delete;
    wire& operator=(const wire&) = delete;

    bool operator==(const wire& other) {
        return p1 == other.p1 && p2 == other.p2;
    }

    void draw(drawer& d);

public:
    glm::ivec2 p1;
    glm::ivec2 p2;
};
