#pragma once

#include "../model/primitive.h"

#include <string>

namespace logic::builtin {

// collects the ports, pins and shapes of a primitive's interface
class builder {
public:
    // width 0 is the normal stroke, see model::bus_stroke for buses
    void line(glm::ivec2 a, glm::ivec2 b, float width = 0.0f) { add({model::no_id, model::line{a, b}, width}); }
    // quadratic curve from a to b with control point c
    void bezier(glm::ivec2 a, glm::ivec2 c, glm::ivec2 b) { add({model::no_id, model::bezier{a, b, c}}); }
    // circular arc from a through c to b
    void arc(glm::ivec2 a, glm::ivec2 c, glm::ivec2 b) { add({model::no_id, model::arc{a, b, c}}); }
    void circle(glm::ivec2 center, float radius) { add({model::no_id, model::circle{center, radius}}); }
    void text(glm::ivec2 pos, std::string str, float size, model::text_align align = model::text_align::left)
    {
        add({model::no_id, model::text{pos, std::move(str), size, align}});
    }

    // lines through the points
    void polyline(std::initializer_list<glm::ivec2> points);
    void rect(glm::ivec2 min, glm::ivec2 max);

    // a port and the pin connecting to it. the reference is valid until the
    // next port.
    model::port& port(std::string name, model::direction dir, int width, glm::ivec2 pin, std::string description = {});

    // a pin of a box part
    struct box_pin {
        std::string name;
        model::direction dir;
        int width = 1;
        std::string description;
        // shown inside the box, the name if empty
        std::string label = {};
        // drawn as thick as a bundle wire
        bool bundle = false;
    };
    // a box with inputs on the left, outputs on the right and more inputs
    // (e.g. selects) at the bottom, each with a stub one grid long and a
    // label inside. the ports are added left first, then bottom, then
    // right, each from the top (bottom: from the left). title is written at
    // the top inside the box. the box is at least min_width grids wide.
    void box(const std::string& title, const std::vector<box_pin>& left, const std::vector<box_pin>& right, const std::vector<box_pin>& bottom = {}, int min_width = 4);

    model::cell_interface finish() { return std::move(r); }

private:
    void add(model::shape s);

    model::cell_interface r;
    model::id next_id = 1;
};

}
