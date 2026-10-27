#include "port_setup.h"

#include <algorithm>
#include <cctype>

namespace logic::netlist {

namespace {

// the name, or the name with 2, 3, ... if another port has it
std::string unique_name(const model::cell& c, const std::string& name, model::id except)
{
    auto taken = [&](const std::string& n) {
        return std::any_of(c.ports.begin(), c.ports.end(), [&](const model::port& p) { return p.id != except && p.name == n; });
    };
    if (!taken(name)) return name;
    for (int n = 2;; n++)
        if (!taken(name + std::to_string(n))) return name + std::to_string(n);
}

}

bool default_port_name(const std::string& name)
{
    for (const char* prefix : {"in", "out", "io"}) {
        std::string p = prefix;
        if (name.size() > p.size() && name.compare(0, p.size(), p) == 0 &&
            std::all_of(name.begin() + p.size(), name.end(), [](char ch) { return std::isdigit((unsigned char)ch) != 0; }))
            return true;
    }
    return false;
}

std::map<model::id, std::set<std::pair<model::id, int>>> linked_pins(const model::cell& c, const netlist& nl)
{
    std::map<model::id, std::set<std::pair<model::id, int>>> r;
    auto sch = c.get_schematic();
    if (!sch) return r;
    for (size_t k = 0; k < c.ports.size() && k < nl.port_nets.size(); k++) {
        auto& pins = r[c.ports[k].id];
        int n = nl.port_nets[k];
        if (n < 0) continue;
        for (auto& t : nl.nets[n].terminals)
            if (t.instance >= 0 && t.instance < (int)sch->instances.size()) pins.insert({sch->instances[t.instance].id, t.port});
    }
    return r;
}

bool adopt_pin_settings(const model::workspace& ws, model::cell& c, const std::vector<model::id>& ports)
{
    auto sch = c.get_schematic();
    if (!sch) return false;
    auto nl = extract(ws, c);

    bool changed = false;
    for (size_t k = 0; k < c.ports.size() && k < nl.port_nets.size(); k++) {
        auto& p = c.ports[k];
        if (std::find(ports.begin(), ports.end(), p.id) == ports.end()) continue;
        int n = nl.port_nets[k];
        if (n < 0) continue;

        std::vector<const model::port*> pins;
        for (auto& t : nl.nets[n].terminals) {
            if (t.instance < 0) continue;
            auto iface = ws.interface_of(sch->instances[t.instance]);
            if (!iface || t.port >= (int)iface.ports->size()) continue;
            auto& pin = (*iface.ports)[t.port];
            if (pin.width > 0) pins.push_back(&pin);
        }
        if (pins.empty()) continue;

        auto& first = *pins[0];
        bool agree = std::all_of(pins.begin(), pins.end(), [&](auto q) {
            return q->width == first.width && q->bundle == first.bundle && (first.bundle == model::no_id || q->flipped == first.flipped);
        });
        if (agree && p.bundle == model::no_id && p.width == 1 && (first.width != 1 || first.bundle != model::no_id)) {
            p.width = first.width;
            p.bundle = first.bundle;
            p.flipped = first.bundle != model::no_id && first.flipped;
            changed = true;
        }
        if (pins.size() != 1) continue;
        if (default_port_name(p.name) && !first.name.empty()) {
            auto name = unique_name(c, first.name, p.id);
            changed |= name != p.name;
            p.name = name;
        }
        if (p.description.empty() && !first.description.empty()) {
            p.description = first.description;
            changed = true;
        }
    }
    return changed;
}

std::vector<model::id> add_ports_for_pins(const model::workspace& ws, model::cell& c, int instance)
{
    std::vector<model::id> added;
    auto sch = c.get_schematic();
    if (!sch || instance < 0 || instance >= (int)sch->instances.size()) return added;
    auto inst = sch->instances[instance];
    auto iface = ws.interface_of(inst);
    if (!iface) return added;
    auto nl = extract(ws, c);
    auto bounds = iface.symbol->bounds();
    constexpr int g = model::grid;

    for (auto& pin : iface.symbol->pins) {
        auto it = std::find_if(iface.ports->begin(), iface.ports->end(), [&](const model::port& p) { return p.id == pin.port; });
        if (it == iface.ports->end()) continue;
        auto index = int(it - iface.ports->begin());
        if (instance < (int)nl.instance_nets.size() && index < (int)nl.instance_nets[instance].size() &&
            nl.instance_nets[instance][index] >= 0)
            continue;
        auto& src = *it;

        // out of the side of the symbol the pin is nearest to
        glm::vec2 s = pin.pos;
        float d[4] = {s.x - bounds.min.x, bounds.max.x - s.x, s.y - bounds.min.y, bounds.max.y - s.y};
        const glm::ivec2 sides[4] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        auto side = sides[std::min_element(d, d + 4) - d];
        auto out = inst.xf.apply(side);

        auto at = model::pin_position(inst, pin);
        auto connect = at + out * (2 * g);
        auto& p = c.add_port(src.dir, connect + out * (2 * g));
        // turned so its connector faces the pin
        for (int r = 0; r < 4; r++) {
            p.xf = {r, false};
            if (model::connection_point(p) == connect) break;
        }
        p.name = unique_name(c, src.name, p.id);
        p.width = std::max(src.width, 1);
        p.bundle = src.bundle;
        p.flipped = src.flipped;
        p.description = src.description;
        p.format = src.format;
        added.push_back(p.id);
        sch->wires.push_back({c.new_id(), at, connect});
    }
    return added;
}

}
