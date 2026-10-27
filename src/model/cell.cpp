#include "cell.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace logic::model {

glm::ivec2 connection_point(const port& p)
{
    glm::ivec2 d{p.dir == direction::input ? 2 * grid : -2 * grid, 0};
    return p.pos + p.xf.apply(d);
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

        if (p.width < 1 || p.width > max_width)
            r.push_back("port '" + p.name + "' width must be between 1 and " + std::to_string(max_width));

        // the automatic symbol has a pin for every port
        if (auto_symbol()) continue;
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

const std::vector<param_def>* cell::external_params() const
{
    return const_cast<cell*>(this)->external_params();
}

std::vector<param_def>* cell::external_params()
{
    if (auto c = std::get_if<c_model_impl>(&impl)) return &c->params;
    if (auto h = std::get_if<hdl_impl>(&impl)) return &h->params;
    return nullptr;
}

std::string field_port_name(const port_group& g, const std::string& path)
{
    auto r = g.field_prefix() + path;
    std::replace(r.begin(), r.end(), '.', '_');
    return r;
}

std::vector<port_prefix> common_prefixes(const std::vector<port>& ports)
{
    std::vector<port_prefix> r;
    for (int k = 0; k < (int)ports.size(); k++) {
        auto& name = ports[k].name;
        auto u = name.rfind('_');
        // a field name is left after the prefix
        if (u == std::string::npos || u == 0 || u + 1 == name.size()) continue;
        auto prefix = name.substr(0, u + 1);
        auto it = std::find_if(r.begin(), r.end(), [&](auto& p) { return p.prefix == prefix; });
        if (it == r.end()) {
            r.push_back({prefix, {}});
            it = r.end() - 1;
        }
        it->ports.push_back(k);
    }
    std::erase_if(r, [](auto& p) { return p.ports.size() < 2; });
    return r;
}

const std::vector<port_group>* cell::port_groups() const
{
    return const_cast<cell*>(this)->port_groups();
}

std::vector<port_group>* cell::port_groups()
{
    if (auto c = std::get_if<c_model_impl>(&impl)) return &c->groups;
    if (auto h = std::get_if<hdl_impl>(&impl)) return &h->groups;
    return nullptr;
}

std::vector<port>* cell::leaf_ports()
{
    if (auto c = std::get_if<c_model_impl>(&impl)) return &c->leaf_ports;
    if (auto h = std::get_if<hdl_impl>(&impl)) return &h->leaf_ports;
    return nullptr;
}

const std::vector<port>& cell::model_ports() const
{
    auto g = port_groups();
    auto l = const_cast<cell*>(this)->leaf_ports();
    return g && !g->empty() && l ? *l : ports;
}

const char* to_string(hdl_language l)
{
    switch (l) {
    case hdl_language::verilog: return "verilog";
    case hdl_language::systemverilog: return "systemverilog";
    case hdl_language::vhdl: return "vhdl";
    }
    return "?";
}

const char* to_string(hdl_backend b)
{
    switch (b) {
    case hdl_backend::verilator: return "verilator";
    case hdl_backend::icarus: return "icarus";
    case hdl_backend::ghdl: return "ghdl";
    }
    return "?";
}

bool supports(hdl_backend b, hdl_language l)
{
    return l == hdl_language::vhdl ? b == hdl_backend::ghdl : b != hdl_backend::ghdl;
}

bool cell::set_ports(const std::vector<port>& list)
{
    bool same = list.size() == ports.size();
    std::vector<port> result;
    for (auto& p : list) {
        auto r = p;
        auto old = std::find_if(ports.begin(), ports.end(), [&](const port& o) { return o.name == p.name; });
        if (old != ports.end()) {
            r.id = old->id;
            r.pos = old->pos;
            r.xf = old->xf;
            if (r.description.empty()) r.description = old->description;
            same = same && old->dir == p.dir;
        } else {
            r.id = new_id();
            same = false;
        }
        result.push_back(std::move(r));
    }

    ports = std::move(result);
    if (same) return false;
    generate_symbol(pin_order::declaration);
    return true;
}

symbol generated_symbol(const std::vector<port>& ports, const std::string& name, pin_order order, const std::function<id()>& new_id)
{
    std::vector<const port*> left, right;
    for (auto& p : ports)
        (p.dir == direction::input ? left : right).push_back(&p);

    if (order == pin_order::position) {
        auto by_pos = [](const port* a, const port* b) { return a->pos.y != b->pos.y ? a->pos.y > b->pos.y : a->pos.x < b->pos.x; };
        std::stable_sort(left.begin(), left.end(), by_pos);
        std::stable_sort(right.begin(), right.end(), by_pos);
    }

    // approximate label width, the model doesn't know font metrics
    constexpr float label_size = 6.0f;
    auto label_w = [&](const std::vector<const port*>& side) {
        size_t n = 0;
        for (auto p : side)
            n = std::max(n, p->name.size());
        return n * label_size * 0.6f;
    };

    // pins are one grid apart from the top, the first at or just above the
    // origin so they stay on the grid
    int rows = (int)std::max<size_t>({left.size(), right.size(), 1});
    int half_w = std::max(2 * grid, (int)std::ceil((label_w(left) + label_w(right) + 12.0f) / 2.0f / grid) * grid);
    int first = (rows - 1) / 2 * grid;
    int top = first + grid;
    int bottom = first - rows * grid;

    symbol r;

    auto add_line = [&](glm::ivec2 a, glm::ivec2 b) { r.shapes.push_back({new_id(), line{a, b}}); };
    add_line({-half_w, top}, {half_w, top});
    add_line({half_w, top}, {half_w, bottom});
    add_line({half_w, bottom}, {-half_w, bottom});
    add_line({-half_w, bottom}, {-half_w, top});

    // pins sit one grid outside the box with a short stub, labels inside
    auto add_side = [&](const std::vector<const port*>& side, bool is_left) {
        int x = is_left ? -half_w : half_w;
        int stub = is_left ? -grid : grid;
        int y = first;
        for (auto p : side) {
            glm::ivec2 pin_pos{x + stub, y};
            r.shapes.push_back({new_id(), line{{x, y}, pin_pos}, p->bundle != no_id ? bundle_stroke : p->width > 1 ? bus_stroke
                                                                                                                        : 0.0f});
            r.pins.push_back({new_id(), p->id, pin_pos});
            // labels against the box: right side ones end there
            int lx = is_left ? x + 3 : x - 3;
            r.shapes.push_back({new_id(), text{{lx, y - 2}, p->name, label_size, is_left ? text_align::left : text_align::right}});
            y -= grid;
        }
    };
    add_side(left, true);
    add_side(right, false);

    r.shapes.push_back({new_id(), text{{-half_w, top + 3}, name, 8.0f}});
    return r;
}

void cell::generate_symbol(pin_order order)
{
    symbol = generated_symbol(ports, name, order, [this] { return new_id(); });
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
