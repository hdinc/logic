#include "builder.h"

#include <algorithm>

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

void builder::box(const std::string& title, const std::vector<box_pin>& left, const std::vector<box_pin>& right, const std::vector<box_pin>& bottom, int min_width)
{
    using model::grid;
    constexpr float label_size = 5.0f;
    auto stroke = [](const box_pin& p) { return p.bundle ? model::bundle_stroke : p.width > 1 ? model::bus_stroke : 0.0f; };

    // rows one grid apart from the top, the first at or above the origin
    int rows = (int)std::max<size_t>({left.size(), right.size(), 1});
    int first = (rows - 1) / 2 * grid;
    int top = first + grid;
    int bottom_y = first - rows * grid;
    int half = std::max({min_width * grid / 2, (int)bottom.size() * grid / 2 + grid, grid});
    half = (half + grid - 1) / grid * grid;
    rect({-half, bottom_y}, {half, top});

    auto label_of = [](const box_pin& p) { return p.label.empty() ? p.name : p.label; };
    int y = first;
    for (auto& p : left) {
        line({-half - grid, y}, {-half, y}, stroke(p));
        text({-half + 2, y - 2}, label_of(p), label_size);
        port(p.name, p.dir, p.width, {-half - grid, y}, p.description);
        y -= grid;
    }
    // bottom pins centered, one grid apart
    int x = -((int)bottom.size() - 1) * grid / 2;
    x = x / grid * grid;
    for (auto& p : bottom) {
        line({x, bottom_y - grid}, {x, bottom_y}, stroke(p));
        text({x, bottom_y + 2}, label_of(p), label_size, model::text_align::center);
        port(p.name, p.dir, p.width, {x, bottom_y - grid}, p.description);
        x += grid;
    }
    y = first;
    for (auto& p : right) {
        line({half, y}, {half + grid, y}, stroke(p));
        text({half - 2, y - 2}, label_of(p), label_size, model::text_align::right);
        port(p.name, p.dir, p.width, {half + grid, y}, p.description);
        y -= grid;
    }
    // inside under the top edge, the instance name goes above the box
    if (!title.empty()) text({0, top - 7}, title, 6.0f, model::text_align::center);
}

model::port& builder::port(std::string name, model::direction dir, int width, glm::ivec2 pin, std::string description)
{
    model::port p;
    p.id = next_id++;
    p.name = std::move(name);
    p.description = std::move(description);
    p.dir = dir;
    p.width = width;
    r.ports.push_back(p);
    r.symbol.pins.push_back({next_id++, p.id, pin});
    return r.ports.back();
}

}
