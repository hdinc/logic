#include "ui_element.h"

spline::spline() : p1 {-1, 0}, p2 {1, 0}, p3 {0, 1} {recalc_path();}
spline::~spline() = default;

void spline::draw(drawer& d) {
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

        if (p3s)
            d.set_color(0, 0, 1, 1);
        else
            d.set_color(0, 0, 0.3, 1);

        d.rect(p3, {0.1, 0.1}, true);
    }

    if(selected) {
        d.set_color(1, 1, 0, 1);
    }
    else {
        d.set_color(0.5725, 0.5725, 0.5725, 1);
    }

    for ( auto i = 0ul; i < path.size()-1; i++) {
        auto p1 = path[i];
        auto p2 = path[i+1];

        d.line(p1, p2);
    }

}

bool spline::try_select(glm::vec2 l) {
    auto dp1 = l - p1;
    auto dp2 = l - p2;
    auto dp3 = l - p3;

    if(dp1.x < 0.1 && dp1.x > -0.1 && dp1.y < 0.1 && dp1.y > -0.1) {
        p1s = true;
        p2s = false;
        p3s = false;
        selected = true;
        return true;
    }
    else if (dp2.x < 0.1 && dp2.x > -0.1 && dp2.y < 0.1 && dp2.y > -0.1) {
        p2s = true;
        p1s = false;
        p3s = false;
        selected = true;
        return true;
    }
    else if (dp3.x < 0.1 && dp3.x > -0.1 && dp3.y < 0.1 && dp3.y > -0.1) {
        p3s = true;
        p1s = false;
        p2s = false;
        selected = true;
        return true;
    }
    else {
        selected = false;
        return false;
    }
}

void spline::move(glm::vec2 l) {
    if (p1s) {
        p1 = l;
    }

    if (p2s) {
        p2 = l;
    }

    if (p3s) {
        p3 = l;
    }

    recalc_path();
}

glm::vec2 spline::get_selection_loc() {
    if(p1s) return p1;
    if(p2s) return p2;
    if(p3s) return p3;
    return loc;
}

void spline::recalc_path() {
    path.clear();

    for(int i = 0; i <= 100; i++) {
        float t = i / 100.0f;
        auto p = p2 + (1-t)*(1-t)*(p1 - p2) + t*t*(p3-p2);
        path.push_back(p);
    }
}
