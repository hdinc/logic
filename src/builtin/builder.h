#pragma once

#include "../model/primitive.h"

#include <string>

namespace logic::builtin {

// collects the ports, pins and shapes of a primitive's interface
class builder {
public:
    void line(glm::ivec2 a, glm::ivec2 b) { add({model::no_id, model::line{a, b}}); }
    // quadratic curve from a to b with control point c
    void bezier(glm::ivec2 a, glm::ivec2 c, glm::ivec2 b) { add({model::no_id, model::bezier{a, b, c}}); }
    void circle(glm::ivec2 center, float radius) { add({model::no_id, model::circle{center, radius}}); }
    void text(glm::ivec2 pos, std::string str, float size) { add({model::no_id, model::text{pos, std::move(str), size}}); }

    // lines through the points
    void polyline(std::initializer_list<glm::ivec2> points);
    void rect(glm::ivec2 min, glm::ivec2 max);

    // a port and the pin connecting to it
    void port(std::string name, model::direction dir, int width, glm::ivec2 pin);

    model::cell_interface finish() { return std::move(r); }

private:
    void add(model::shape s);

    model::cell_interface r;
    model::id next_id = 1;
};

}
