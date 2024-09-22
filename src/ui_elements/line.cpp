#include "ui_element.h"

line::line() : p1 {-1, 0}, p2 {1, 0} {}
line::~line() = default;

void line::draw(drawer& d) {

    if(selected) {
        if (p1s)
            d.set_color(0, 0, 1, 1);
        else
            d.set_color(0, 0, 0.3, 1);

        d.rect(p1, {0.1, 0.1}, true);

        if (p2s)
            d.set_color(0, 0, 1, 1);
        else
            d.set_color(0, 0, 0.3, 1);

        d.rect(p2, {0.1, 0.1}, true);
    }

    if(selected) {
        d.set_color(1, 1, 0, 1);
    }
    else {
        d.set_color(0.5725, 0.5725, 0.5725, 1);
    }
    d.line(p1, p2);
}

bool line::try_select(glm::vec2 l) {
    auto dp1 = l - p1;
    auto dp2 = l - p2;
    if(dp1.x < 0.1 && dp1.x > -0.1 && dp1.y < 0.1 && dp1.y > -0.1) {
        p1s = true;
        p2s = false;
        selected = true;
        return true;
    }
    else if (dp2.x < 0.1 && dp2.x > -0.1 && dp2.y < 0.1 && dp2.y > -0.1) {
        p2s = true;
        p1s = false;
        selected = true;
        return true;
    }
    else {
        selected = false;
        return false;
    }
}

void line::move(glm::vec2 l) {
    if (p1s) {
        p1 = l;
    }

    if (p2s) {
        p2 = l;
    }
}

glm::vec2 line::get_selection_loc() {
    if(p1s) return p1;
    if(p2s) return p2;
    return loc;
}
