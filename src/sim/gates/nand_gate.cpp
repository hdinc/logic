#include "nand_gate.h"
#include "../../draw/bezier.h"
#include "../pin.h"

nand_gate::nand_gate() {
    // pin in0  {{-2,  1}, &default_net, 0, 0, this};
    // pin in1  {{-2, -1}, &default_net, 1, 0, this};
    // pin out0 {{ 2,  0}, &default_net, 0, 1, this};

    pin in0, in1, out0;

    in0.g = this;
    in1.g = this;
    out0.g = this;

    pins.push_back(in0);
    pins.push_back(in1);
    pins.push_back(out0);

    pin_positions.push_back({-2,  1});
    pin_positions.push_back({-2, -1});
    pin_positions.push_back({-2,  0});

    box = {2, 2};
}

void nand_gate::update() {
    logic_value a = pins[0].get_value();
    logic_value b = pins[1].get_value();

    logic_value c;

    c.v = ~(a.v & b.v);
    c.z = a.z;

    need_update = 0;

    pins[2].set_value(c);
}

void nand_gate::draw(drawer& d) {

    if(selected) {
        d.set_color(1, 1, 0, 1);
    } else if (need_update) {
        d.set_color(0.6, 0.3, 0, 1);
    }else {
        d.set_color(0.5725, 0.5725, 0.5725, 1);
    }

    d.line(glm::vec2{pos} + glm::vec2{-2, -1}, glm::vec2{pos} + glm::vec2{-1.3, -1});
    d.line(glm::vec2{pos} + glm::vec2{-2,  1}, glm::vec2{pos} + glm::vec2{-1.3,  1});
    d.line(glm::vec2{pos} + glm::vec2{-1.3,  1.4}, glm::vec2{pos} + glm::vec2{-0.5,  1.4});
    d.line(glm::vec2{pos} + glm::vec2{-1.3,  -1.4}, glm::vec2{pos} + glm::vec2{-0.5,  -1.4});
    d.line(glm::vec2{pos} + glm::vec2{-1.3,  1.4}, glm::vec2{pos} + glm::vec2{-1.3,  -1.4});
    d.line(glm::vec2{pos} + glm::vec2{ 2,  0}, glm::vec2{pos} + glm::vec2{1.45,  0});

    d.circle(glm::vec2{pos} + glm::vec2{1.25, 0}, 0.2, false);

    draw_bezier(d, glm::vec2{pos} + glm::vec2 {0.6, 1}, glm::vec2{pos} + glm::vec2 {0.2, 1.4}, glm::vec2{pos} + glm::vec2 {-0.5, 1.4});
    draw_bezier(d, glm::vec2{pos} + glm::vec2 {0.6, -1}, glm::vec2{pos} + glm::vec2 {1.5, 0}, glm::vec2{pos} + glm::vec2 {0.6, 1});
    draw_bezier(d, glm::vec2{pos} + glm::vec2 {-0.5, -1.4}, glm::vec2{pos} + glm::vec2 {0.2, -1.4}, glm::vec2{pos} + glm::vec2 {0.6, -1});

    if (pins[0].n == &default_net) {
        d.circle(glm::vec2{pos} + glm::vec2{-2,  1}, 0.2, true);
    }

    if (pins[1].n == &default_net) {
        d.circle(glm::vec2{pos} + glm::vec2{-2, -1}, 0.2, true);
    }

    if (pins[2].n == &default_net) {
        d.circle(glm::vec2{pos} + glm::vec2{2, 0}, 0.2, true);
    }

}

nand_gate::~nand_gate() {
}
