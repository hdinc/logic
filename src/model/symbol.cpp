#include "symbol.h"

#include <cmath>
#include <numbers>

namespace logic::model {

namespace {
template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
}

int point_count(const shape& s)
{
    return std::visit(overloaded{
                          [](const line&) { return 2; },
                          [](const bezier&) { return 3; },
                          [](const arc&) { return 3; },
                          [](const circle&) { return 2; },
                          [](const text&) { return 1; },
                      },
                      s.geom);
}

glm::ivec2 get_point(const shape& s, int i)
{
    return std::visit(overloaded{
                          [&](const line& l) { return i == 0 ? l.a : l.b; },
                          [&](const bezier& b) { return i == 0 ? b.a : (i == 1 ? b.b : b.c); },
                          [&](const arc& a) { return i == 0 ? a.a : (i == 1 ? a.b : a.c); },
                          [&](const circle& c) { return i == 0 ? c.center : c.center + glm::ivec2(glm::round(c.radius), 0); },
                          [&](const text& t) { return t.pos; },
                      },
                      s.geom);
}

void set_point(shape& s, int i, glm::ivec2 p)
{
    std::visit(overloaded{
                   [&](line& l) { (i == 0 ? l.a : l.b) = p; },
                   [&](bezier& b) { (i == 0 ? b.a : (i == 1 ? b.b : b.c)) = p; },
                   [&](arc& a) { (i == 0 ? a.a : (i == 1 ? a.b : a.c)) = p; },
                   [&](circle& c) {
                       if (i == 0)
                           c.center = p;
                       else
                           c.radius = glm::length(glm::vec2(p - c.center));
                   },
                   [&](text& t) { t.pos = p; },
               },
               s.geom);
}

std::vector<glm::vec2> arc_points(const arc& s)
{
    glm::dvec2 a = s.a, b = s.b, c = s.c;

    // center of the circle through the three points
    double d = 2.0 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
    if (std::abs(d) < 1e-9) return {glm::vec2(a), glm::vec2(b)};
    double aa = glm::dot(a, a), bb = glm::dot(b, b), cc = glm::dot(c, c);
    glm::dvec2 m{(aa * (b.y - c.y) + bb * (c.y - a.y) + cc * (a.y - b.y)) / d, (aa * (c.x - b.x) + bb * (a.x - c.x) + cc * (b.x - a.x)) / d};
    double r = glm::length(a - m);

    constexpr double tau = 2.0 * std::numbers::pi;
    auto angle = [&](glm::dvec2 p) { return std::atan2(p.y - m.y, p.x - m.x); };
    auto wrap = [&](double t) { return t - tau * std::floor(t / tau); };
    double ta = angle(a);
    // counter clockwise to b, unless c is on the other way
    double sweep = wrap(angle(b) - ta);
    if (wrap(angle(c) - ta) > sweep) sweep -= tau;

    int n = std::max(4, (int)std::ceil(std::abs(sweep) / tau * 72.0));
    std::vector<glm::vec2> r_points;
    r_points.push_back(glm::vec2(a));
    for (int k = 1; k < n; k++) {
        double t = ta + sweep * k / n;
        r_points.push_back(glm::vec2(m + r * glm::dvec2{std::cos(t), std::sin(t)}));
    }
    r_points.push_back(glm::vec2(b));
    return r_points;
}

shape* symbol::find_shape(id i)
{
    for (auto& s : shapes)
        if (s.id == i) return &s;
    return nullptr;
}

pin* symbol::find_pin(id i)
{
    for (auto& p : pins)
        if (p.id == i) return &p;
    return nullptr;
}

const pin* symbol::find_pin_of_port(id port) const
{
    for (auto& p : pins)
        if (p.port == port) return &p;
    return nullptr;
}

rect symbol::bounds() const
{
    bool first = true;
    rect r;

    auto add = [&](glm::vec2 p) {
        if (first) {
            r = {p, p};
            first = false;
        } else {
            r.expand(p);
        }
    };

    for (auto& s : shapes) {
        if (auto c = std::get_if<circle>(&s.geom)) {
            add(glm::vec2(c->center) - c->radius);
            add(glm::vec2(c->center) + c->radius);
        } else if (auto a = std::get_if<arc>(&s.geom)) {
            for (auto p : arc_points(*a))
                add(p);
        } else {
            for (int i = 0; i < point_count(s); i++)
                add(get_point(s, i));
        }
    }

    for (auto& p : pins)
        add(p.pos);

    if (first) return {{-grid, -grid}, {grid, grid}};

    return r;
}

}
