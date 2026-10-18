#include "builder.h"

namespace logic::builtin {

void builder::add(model::shape s)
{
    s.id = next_id++;
    r.symbol.shapes.push_back(std::move(s));
}

void builder::polyline(std::initializer_list<glm::ivec2> points)
{
    auto it = points.begin();
    for (auto prev = it++; it != points.end(); prev = it++)
        line(*prev, *it);
}

void builder::rect(glm::ivec2 min, glm::ivec2 max)
{
    polyline({min, {max.x, min.y}, max, {min.x, max.y}, min});
}

void builder::port(std::string name, model::direction dir, int width, glm::ivec2 pin)
{
    model::port p;
    p.id = next_id++;
    p.name = std::move(name);
    p.dir = dir;
    p.width = width;
    r.ports.push_back(p);
    r.symbol.pins.push_back({next_id++, p.id, pin});
}

}
