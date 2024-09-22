#include "input_gate.h"
#include "../pin.h"

input_gate::input_gate() {
    // pin out0 {{ 2,  0}, &default_net, 0, 1, this};

    pin out0;

    pin_positions.push_back({2, 0});
    out0.g = this;

    pins.push_back(out0);

    box = {2, 1};
}

void input_gate::update() {
    need_update = 0;

    pins[0].set_value({val, pins[0].lv.z});
}

void input_gate::draw(drawer& d) {

    if(selected) {
        d.set_color(1, 1, 0, 1);
    } else if (need_update) {
        d.set_color(0.6, 0.3, 0, 1);
    }else {
        d.set_color(0.5725, 0.5725, 0.5725, 1);
    }

    d.rect(glm::vec2{pos}, {0.5, 0.5}, false);
    d.line(glm::vec2{pos} + glm::vec2 {0.5, 0}, glm::vec2{pos} + glm::vec2 {2, 0});
    if (pins[0].n == &default_net) {
        d.circle(glm::vec2{pos} + glm::vec2{2, 0}, 0.2, true);
    }

    d.text("a", {pos.x-2, pos.y}, 2);

}

input_gate::~input_gate() {
}
