#include "simulator.h"

#include <algorithm>

namespace logic::sim {

const scope* scope::child(int instance) const
{
    int k = child_index[instance];
    return k < 0 ? nullptr : children[k].get();
}

simulator::simulator(const model::workspace& w, const model::cell& top, const component_factory& f)
    : ws(w)
    , factory(f)
    , m_root(std::make_unique<scope>())
    , top_ports(top.ports)
{
    elaborate(*m_root, top, std::vector<int>(top.ports.size(), -1), 0);
    finish_nets();

    net_states.resize(nets.size());

    for (int k = 0; k < (int)top_ports.size(); k++) {
        auto& p = top_ports[k];
        inputs.push_back(p.dir == model::direction::input ? known(0, p.width) : floating(p.width));
        int n = net_of_port(*m_root, k);
        if (n >= 0 && p.dir != model::direction::output) net_states[n].port_drivers.push_back(k);
    }

    for (int i = 0; i < (int)instances.size(); i++) {
        auto& s = instances[i];
        for (int p = 0; p < (int)s.nets.size(); p++) {
            int n = s.nets[p];
            if (n < 0) continue;
            if (s.is_output[p]) net_states[n].drivers.push_back({i, p});
            // inouts are treated as outputs until there are tri-state primitives
            if (!s.is_output[p]) {
                auto& readers = net_states[n].readers;
                if (std::find(readers.begin(), readers.end(), i) == readers.end()) readers.push_back(i);
            }
        }
    }

    queued.assign(instances.size(), false);
    reset();
}

const netlist::netlist& simulator::netlist_of(const model::cell& c)
{
    auto& nl = netlists[&c];
    if (!nl) {
        nl = std::make_unique<netlist::netlist>(netlist::extract(ws, c));
        // reported once per cell, not for every instance
        for (auto& p : nl->problems)
            m_problems.push_back(&c == m_root->cell ? p : "in '" + c.name + "': " + p);
    }
    return *nl;
}

void simulator::elaborate(scope& s, const model::cell& c, const std::vector<int>& port_nets, int depth)
{
    s.cell = &c;
    if (std::find(m_cells.begin(), m_cells.end(), c.id) == m_cells.end()) m_cells.push_back(c.id);

    auto& nl = netlist_of(c);
    s.nets = &nl;

    s.flat_net.resize(nl.nets.size());
    for (size_t n = 0; n < nl.nets.size(); n++)
        s.flat_net[n] = new_net(nl.nets[n].width, nl.nets[n].width_error);

    // the cell's ports are the nets at the instance's pins in the parent
    for (size_t k = 0; k < c.ports.size(); k++)
        if (port_nets[k] >= 0 && nl.port_nets[k] >= 0) join(port_nets[k], s.flat_net[nl.port_nets[k]]);

    auto sch = c.get_schematic();
    if (!sch) return;

    auto where = [&](const model::instance& inst) { return "instance " + (s.path.empty() ? inst.name : s.path + "/" + inst.name); };

    s.components.assign(sch->instances.size(), -1);
    s.child_index.assign(sch->instances.size(), -1);

    for (int i = 0; i < (int)sch->instances.size(); i++) {
        auto& inst = sch->instances[i];
        auto iface = ws.interface_of(inst);
        auto cell = ws.find_cell(inst.cell);
        // missing cells are already reported by the netlist
        if (!iface || !cell) continue;

        std::vector<int> pins(iface.ports->size(), -1);
        for (size_t p = 0; p < pins.size(); p++) {
            int local = nl.instance_nets[i][p];
            if (local >= 0) pins[p] = s.flat_net[local];
        }

        if (auto prim = ws.primitive_of(*cell)) {
            auto comp = factory.create(prim->kind(), model::resolve_params(prim->params(), inst.params));
            if (!comp) {
                m_problems.push_back(where(inst) + ": '" + cell->name + "' has no simulation model");
                continue;
            }

            instance x;
            x.c = std::move(comp);
            x.nets = pins;
            for (auto& port : *iface.ports) {
                x.widths.push_back(port.width);
                x.ports.push_back(floating(port.width));
                x.is_output.push_back(port.dir != model::direction::input);
            }
            s.components[i] = (int)instances.size();
            instances.push_back(std::move(x));
        } else if (cell->get_schematic()) {
            if (depth >= max_depth) {
                m_problems.push_back(where(inst) + ": nested too deep, does '" + cell->name + "' contain itself?");
                continue;
            }

            auto child = std::make_unique<scope>();
            child->name = inst.name;
            child->path = s.path.empty() ? inst.name : s.path + "/" + inst.name;
            child->parent = &s;
            // the interface ports of a schematic cell are its own ports
            elaborate(*child, *cell, pins, depth + 1);
            s.child_index[i] = (int)s.children.size();
            s.children.push_back(std::move(child));
        } else {
            m_problems.push_back(where(inst) + ": '" + cell->name + "' has no simulation model");
        }
    }
}

int simulator::new_net(int width, bool width_error)
{
    parent.push_back((int)parent.size());
    nets.push_back({width, width_error});
    return parent.back();
}

int simulator::find(int n)
{
    while (parent[n] != n)
        n = parent[n] = parent[parent[n]];
    return n;
}

void simulator::join(int a, int b)
{
    a = find(a);
    b = find(b);
    if (a == b) return;
    parent[a] = b;

    auto& x = nets[a];
    auto& y = nets[b];
    if (x.width && y.width && x.width != y.width) y.width_error = true;
    y.width = std::max(x.width, y.width);
    y.width_error = y.width_error || x.width_error;
}

void simulator::finish_nets()
{
    // one net per set of joined nets
    std::vector<int> index(parent.size(), -1);
    std::vector<net_info> merged;
    for (int n = 0; n < (int)parent.size(); n++) {
        int r = find(n);
        if (index[r] < 0) {
            index[r] = (int)merged.size();
            merged.push_back(nets[r]);
        }
        index[n] = index[r];
    }
    nets = std::move(merged);

    auto remap = [&](std::vector<int>& v) {
        for (auto& n : v)
            if (n >= 0) n = index[n];
    };
    for (auto& i : instances)
        remap(i.nets);

    std::vector<scope*> stack{m_root.get()};
    while (!stack.empty()) {
        auto s = stack.back();
        stack.pop_back();
        remap(s->flat_net);
        for (auto& c : s->children)
            stack.push_back(c.get());
    }

    parent.clear();
}

int simulator::net_of_wire(const scope& s, model::id wire) const
{
    int local = s.nets->net_of_wire(wire);
    return local < 0 ? -1 : s.flat_net[local];
}

int simulator::net_of_port(const scope& s, int port) const
{
    int local = s.nets->port_nets[port];
    return local < 0 ? -1 : s.flat_net[local];
}

value simulator::port_value(const scope& s, int port) const
{
    int n = net_of_port(s, port);
    return n < 0 ? floating(s.cell->ports[port].width) : net_states[n].v;
}

void simulator::set_input(int port, value v)
{
    auto m = mask(top_ports[port].width);
    inputs[port] = {v.bits & m, v.x & m, v.z & m};

    int n = net_of_port(*m_root, port);
    if (n >= 0) update_net(n);
    settle();
}

void simulator::tick()
{
    tick_count++;
    for (int i = 0; i < (int)instances.size(); i++)
        if (instances[i].c && instances[i].c->tick()) schedule(i);
    settle();
}

void simulator::reset()
{
    tick_count = 0;

    for (int i = 0; i < (int)instances.size(); i++) {
        auto& s = instances[i];
        if (!s.c) continue;
        s.c->reset();
        for (size_t p = 0; p < s.ports.size(); p++)
            s.ports[p] = floating(s.widths[p]);
        schedule(i);
    }

    for (int n = 0; n < (int)net_states.size(); n++) {
        net_states[n].v = floating(nets[n].width);
        update_net(n);
    }

    settle();
}

void simulator::schedule(int instance)
{
    if (queued[instance]) return;
    queued[instance] = true;
    queue.push_back(instance);
}

void simulator::update_net(int n)
{
    auto& net = nets[n];
    auto& st = net_states[n];

    value v;
    if (net.width_error) {
        v = unknown(net.width);
    } else {
        std::vector<value> drivers;
        for (auto [i, p] : st.drivers)
            drivers.push_back(instances[i].ports[p]);
        for (int k : st.port_drivers)
            drivers.push_back(inputs[k]);
        v = resolve(drivers, net.width);
    }

    if (v == st.v) return;
    st.v = v;
    for (int r : st.readers)
        schedule(r);
}

void simulator::settle()
{
    m_oscillating = false;

    std::vector<int> batch;
    std::vector<int> changed;
    std::vector<value> before;

    for (int step = 0; !queue.empty(); step++) {
        if (step == max_steps) {
            m_oscillating = true;
            for (int i : queue)
                queued[i] = false;
            queue.clear();
            break;
        }

        // everything scheduled reacts to the values of the previous step
        batch.swap(queue);
        queue.clear();
        for (int i : batch)
            queued[i] = false;

        changed.clear();
        for (int i : batch) {
            auto& s = instances[i];
            // outputs keep what eval wrote last time. inouts are treated as
            // outputs until there are tri-state primitives.
            for (size_t p = 0; p < s.ports.size(); p++) {
                if (s.is_output[p]) continue;
                int n = s.nets[p];
                if (n < 0)
                    s.ports[p] = floating(s.widths[p]);
                else if (nets[n].width_error)
                    s.ports[p] = unknown(s.widths[p]);
                else
                    s.ports[p] = net_states[n].v;
            }

            before = s.ports;
            s.c->eval(s.ports);

            for (size_t p = 0; p < s.ports.size(); p++) {
                auto m = mask(s.widths[p]);
                auto& v = s.ports[p];
                v = {v.bits & m, v.x & m, v.z & m};
                if (s.is_output[p] && v != before[p] && s.nets[p] >= 0) changed.push_back(s.nets[p]);
            }
        }

        for (int n : changed)
            update_net(n);
    }
}

}
