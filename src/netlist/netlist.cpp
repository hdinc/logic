#include "netlist.h"

namespace logic::netlist {

namespace {

struct ivec2_less {
    bool operator()(glm::ivec2 a, glm::ivec2 b) const { return a.x != b.x ? a.x < b.x : a.y < b.y; }
};

class union_find {
public:
    int add()
    {
        parent.push_back((int)parent.size());
        return parent.back();
    }

    int find(int i)
    {
        while (parent[i] != i)
            i = parent[i] = parent[parent[i]];
        return i;
    }

    void join(int a, int b) { parent[find(a)] = find(b); }

private:
    std::vector<int> parent;
};

}

int netlist::net_of_wire(model::id w) const
{
    auto it = wire_net.find(w);
    return it == wire_net.end() ? -1 : it->second;
}

netlist extract(const model::workspace& ws, const model::cell& c)
{
    netlist r;
    auto sch = c.get_schematic();
    if (!sch) return r;

    // one node per distinct point, everything at a point is connected
    union_find uf;
    std::map<glm::ivec2, int, ivec2_less> nodes;
    auto node = [&](glm::ivec2 p) {
        auto it = nodes.find(p);
        if (it == nodes.end()) it = nodes.emplace(p, uf.add()).first;
        return it->second;
    };

    for (auto& w : sch->wires)
        uf.join(node(w.a), node(w.b));

    struct placed {
        terminal t;
        int node;
        int width;
        model::id bundle;
        std::string name;
    };
    std::vector<placed> terms;

    for (int i = 0; i < (int)c.ports.size(); i++) {
        auto& p = c.ports[i];
        terms.push_back({{-1, i}, node(model::connection_point(p)), p.width, p.bundle, p.name});
        if (p.bundle != model::no_id && !ws.find_bundle(p.bundle))
            r.problems.push_back("port " + p.name + ": bundle type " + ws.bundle_label(p.bundle) + " is missing");
    }

    r.instance_nets.resize(sch->instances.size());
    for (int i = 0; i < (int)sch->instances.size(); i++) {
        auto& inst = sch->instances[i];
        auto iface = ws.interface_of(inst);
        if (!iface) {
            r.problems.push_back("instance " + inst.name + ": the cell is missing");
            continue;
        }

        r.instance_nets[i].assign(iface.ports->size(), -1);
        for (auto& pin : iface.symbol->pins) {
            for (int k = 0; k < (int)iface.ports->size(); k++) {
                auto& port = (*iface.ports)[k];
                if (port.id != pin.port) continue;
                terms.push_back({{i, k}, node(model::pin_position(inst, pin)), port.width, port.bundle, inst.name + "." + port.name});
            }
        }
    }

    // tunnels of the same name are connected
    std::map<std::string, int> tunnel_node;
    for (int i = 0; i < (int)sch->instances.size(); i++) {
        auto& inst = sch->instances[i];
        auto iface = ws.interface_of(inst);
        if (!iface || !iface.tunnel || iface.tunnel->empty() || iface.symbol->pins.empty()) continue;
        int n = node(model::pin_position(inst, iface.symbol->pins[0]));
        auto [it, added] = tunnel_node.emplace(*iface.tunnel, n);
        if (!added) uf.join(n, it->second);
    }

    // nets in order of their first wire or terminal, so indices are stable
    // for the same schematic
    std::map<int, int> root_net;
    auto net_of_node = [&](int n) {
        int root = uf.find(n);
        auto it = root_net.find(root);
        if (it == root_net.end()) {
            it = root_net.emplace(root, (int)r.nets.size()).first;
            r.nets.emplace_back();
        }
        return it->second;
    };

    for (auto& w : sch->wires) {
        int n = net_of_node(node(w.a));
        r.nets[n].wires.push_back(w.id);
        r.wire_net[w.id] = n;
    }

    // a terminal alone at its point (no wire, no other terminal) is unconnected
    std::map<int, int> terms_at_node;
    for (auto& t : terms)
        terms_at_node[t.node]++;

    r.port_nets.assign(c.ports.size(), -1);
    std::vector<std::string> first_name;
    for (auto& t : terms) {
        int root = uf.find(t.node);
        bool has_wire = root_net.count(root) > 0;
        if (!has_wire && terms_at_node[t.node] < 2) continue;

        int n = net_of_node(t.node);
        auto& net = r.nets[n];
        net.terminals.push_back(t.t);
        if (t.t.instance < 0)
            r.port_nets[t.t.port] = n;
        else
            r.instance_nets[t.t.instance][t.t.port] = n;

        first_name.resize(r.nets.size());
        // a terminal of width 0 (a tunnel) takes the net's width
        if (t.width == 0) continue;
        auto type = [&](int width, model::id bundle) { return bundle != model::no_id ? ws.bundle_label(bundle) : std::to_string(width) + (width == 1 ? " bit" : " bits"); };
        if (net.width == 0) {
            net.width = t.width;
            net.bundle = t.bundle;
            first_name[n] = t.name;
        } else if (net.width != t.width && !net.width_error) {
            net.width_error = true;
            r.problems.push_back("width mismatch: " + first_name[n] + " has " + std::to_string(net.width) + " bits, " + t.name + " has " + std::to_string(t.width));
        } else if (net.width == t.width && net.bundle != t.bundle && !net.type_error && !net.width_error) {
            net.type_error = true;
            r.problems.push_back("type mismatch: " + first_name[n] + " is " + type(net.width, net.bundle) + ", " + t.name + " is " + type(t.width, t.bundle));
        }
    }

    return r;
}

}
