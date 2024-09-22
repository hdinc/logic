#include "ui_element.h"

void anchor::draw(drawer& d) {
    if(selected) {
        d.set_color(1, 1, 0, 1);
    }
    else {
        d.set_color(0.5725, 0.5725, 0.5725, 1);
    }

    d.line(loc + glm::vec2{0.5,0}, loc + glm::vec2{-0.5,0});
    d.line(loc + glm::vec2{0,0.5}, loc + glm::vec2{0,-0.5});
    d.line(loc + glm::vec2{-0.2, -0.2}, loc + glm::vec2{-0.2,  0.2});
    d.line(loc + glm::vec2{-0.2,  0.2}, loc + glm::vec2{ 0.2,  0.2});
    d.line(loc + glm::vec2{ 0.2,  0.2}, loc + glm::vec2{ 0.2, -0.2});
    d.line(loc + glm::vec2{ 0.2, -0.2}, loc + glm::vec2{-0.2, -0.2});
}

anchor::~anchor() = default;

bool anchor::try_select(glm::vec2 l) {
    l = l - loc;
    if(l.x < 0.2 && l.x > -0.2 && l.y < 0.2 && l.y > -0.2) {
        selected = true;
        return true;
    }
    selected = false;
    return false;
}

glm::vec2 anchor::get_selection_loc() {
    return loc;
}

void anchor::move(glm::vec2 l) {
    loc = l;
}
