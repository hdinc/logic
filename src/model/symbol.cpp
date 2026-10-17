#include "symbol.h"

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
