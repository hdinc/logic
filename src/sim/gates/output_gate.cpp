#include "output_gate.h"
#include "../pin.h"

output_gate::output_gate() {
    pin in0;
    in0.g = this;

    pins.push_back(in0);
    pin_positions.push_back({-2, 0});

    box = {2, 1};
}

void output_gate::update() {
    need_update = 0;
    logic_value v = pins[0].get_value();
    val = v.v;
}

void output_gate::draw(drawer& d) {

    if(selected || val) {
        d.set_color(1, 1, 0, 1);
    } else if (need_update) {
        d.set_color(0.6, 0.3, 0, 1);
    }else {
        d.set_color(0.5725, 0.5725, 0.5725, 1);
    }

    d.circle(glm::vec2{pos}, 0.5, false);
    d.line(glm::vec2{pos} + glm::vec2 {-0.5, 0}, glm::vec2{pos} + glm::vec2 {-2, 0});

    if (pins[0].n == &default_net) {
        d.circle(glm::vec2{pos} + glm::vec2{-2, 0}, 0.2, true);
    }

}

output_gate::~output_gate() {
}
