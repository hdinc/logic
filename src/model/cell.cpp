#include "cell.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace logic::model {

glm::ivec2 connection_point(const port& p)
{
    if (p.dir == direction::input)
        return p.pos + glm::ivec2{2 * grid, 0};
    return p.pos - glm::ivec2{2 * grid, 0};
}

port* cell::find_port(model::id i)
{
    for (auto& p : ports)
        if (p.id == i) return &p;
    return nullptr;
}

const port* cell::find_port(model::id i) const
{
    for (auto& p : ports)
        if (p.id == i) return &p;
    return nullptr;
}

port& cell::add_port(direction dir, glm::ivec2 pos)
{
    port p;
    p.id = new_id();
    p.dir = dir;
    p.pos = pos;
    p.name = unique_port_name(dir == direction::input ? "in" : (dir == direction::output ? "out" : "io"));
    ports.push_back(std::move(p));
    return ports.back();
}

void cell::remove_port(model::id i)
{
    std::erase_if(ports, [&](const port& p) { return p.id == i; });
    std::erase_if(symbol.pins, [&](const pin& p) { return p.port == i; });
}

std::string cell::unique_port_name(const std::string& prefix) const
{
    for (int n = 1;; n++) {
        auto name = prefix + std::to_string(n);
        if (std::none_of(ports.begin(), ports.end(), [&](const port& p) { return p.name == name; }))
            return name;
    }
}

std::vector<std::string> cell::check() const
{
    std::vector<std::string> r;

    std::set<std::string> names;
    for (auto& p : ports) {
        if (p.name.empty())
            r.push_back("a port has no name");
        else if (!names.insert(p.name).second)
            r.push_back("duplicate port name '" + p.name + "'");

        if (p.width < 1 || p.width > 64)
            r.push_back("port '" + p.name + "' width must be between 1 and 64");

        auto n = std::count_if(symbol.pins.begin(), symbol.pins.end(), [&](const pin& s) { return s.port == p.id; });
        if (n == 0)
            r.push_back("port '" + p.name + "' has no pin on the symbol");
        else if (n > 1)
            r.push_back("port '" + p.name + "' has more than one pin on the symbol");
    }

    for (auto& s : symbol.pins)
        if (!find_port(s.port))
            r.push_back("a symbol pin is not assigned to a port");

    return r;
}

void cell::generate_symbol()
{
    std::vector<const port*> left, right;
    for (auto& p : ports)
        (p.dir == direction::input ? left : right).push_back(&p);

    auto by_pos = [](const port* a, const port* b) { return a->pos.y != b->pos.y ? a->pos.y > b->pos.y : a->pos.x < b->pos.x; };
    std::sort(left.begin(), left.end(), by_pos);
    std::sort(right.begin(), right.end(), by_pos);

    // approximate label width, the model doesn't know font metrics
    constexpr float label_size = 6.0f;
    auto label_w = [&](const std::vector<const port*>& side) {
        size_t n = 0;
        for (auto p : side)
            n = std::max(n, p->name.size());
        return n * label_size * 0.6f;
    };

    int rows = (int)std::max<size_t>({left.size(), right.size(), 1});
    int half_w = std::max(2 * grid, (int)std::ceil((label_w(left) + label_w(right) + 12.0f) / 2.0f / grid) * grid);
    int top = rows * grid;
    int bottom = -rows * grid;

    symbol = {};

    auto add_line = [&](glm::ivec2 a, glm::ivec2 b) { symbol.shapes.push_back({new_id(), line{a, b}}); };
    add_line({-half_w, top}, {half_w, top});
    add_line({half_w, top}, {half_w, bottom});
    add_line({half_w, bottom}, {-half_w, bottom});
    add_line({-half_w, bottom}, {-half_w, top});

    // pins sit one grid outside the box with a short stub, labels inside
    auto add_side = [&](const std::vector<const port*>& side, bool is_left) {
        int x = is_left ? -half_w : half_w;
        int stub = is_left ? -grid : grid;
        // pins are two grid apart and centered on the origin
        int y = ((int)side.size() - 1) * grid;
        for (auto p : side) {
            glm::ivec2 pin_pos{x + stub, y};
            add_line({x, y}, pin_pos);
            symbol.pins.push_back({new_id(), p->id, pin_pos});
            int lx = is_left ? x + 3 : x - 3 - (int)std::ceil(p->name.size() * label_size * 0.6f);
            symbol.shapes.push_back({new_id(), text{{lx, y - 2}, p->name, label_size}});
            y -= 2 * grid;
        }
    };
    add_side(left, true);
    add_side(right, false);

    symbol.shapes.push_back({new_id(), text{{-half_w, top + 3}, name, 8.0f}});
}

const char* to_string(direction d)
{
    switch (d) {
    case direction::input: return "input";
    case direction::output: return "output";
    case direction::inout: return "inout";
    }
    return "?";
}

glm::ivec2 pin_position(const instance& i, const pin& p)
{
    return i.pos + i.xf.apply(p.pos);
}

rect instance_bounds(const instance& i, const symbol* s)
{
    rect r = s ? s->bounds() : rect{{-grid, -grid}, {grid, grid}};
    auto a = glm::vec2(i.pos) + i.xf.apply(r.min);
    auto b = glm::vec2(i.pos) + i.xf.apply(r.max);
    return make_rect(a, b);
}

}
