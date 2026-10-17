#pragma once

#include <vector>
#include <string>
#include <Hd.h>

namespace logic::editor::symbol {

struct line {
    glm::ivec2 p1;
    glm::ivec2 p2;
};

struct bezier {
    glm::ivec2 p1;
    glm::ivec2 p2;
    glm::ivec2 p3;
};

struct circle {
    glm::ivec2 p1;
    glm::ivec2 p2;
};

struct text {
    std::string t;
    float size;
    glm::ivec2 pos;
};

struct pin {
    std::string name;
    int number;
    glm::ivec2 pos;
};

class symbol {
public:
    void draw(glm::mat4& vp, Hd::Graphics2D& g, glm::ivec2 pos);

    glm::vec2 box = {1, 1};

    std::vector<line> lines;
    std::vector<bezier> beziers;
    std::vector<circle> circles;
    std::vector<text> texts;
    std::vector<pin> pins;
};

}
