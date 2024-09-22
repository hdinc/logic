#include "ui_element.h"

void pin::draw(drawer& d) {
    if(selected) {
        d.set_color(1, 1, 0, 1);
    }
    else {
        d.set_color(0.5725, 0.5725, 0.5725, 1);
    }

    d.circle(loc, 0.2, true);

}

pin::~pin() = default;

bool pin::try_select(glm::vec2 l) {
    if ( glm::length(l - loc) < 0.2) {
        selected = true;
        return true;
    }

    selected = false;
    return false;
}

void pin::move(glm::vec2 l) {
    loc = l;
}

glm::vec2 pin::get_selection_loc() {
    return loc;
}
