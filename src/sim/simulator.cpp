#include "simulator.h"

#include <algorithm>

namespace logic::sim {

simulator::simulator(const model::workspace& ws, const model::cell& top, const component_factory& f)
    : m_nets(netlist::extract(ws, top))
    , m_problems(m_nets.problems)
    , top_ports(top.ports)
{
    net_states.resize(m_nets.nets.size());

    for (int k = 0; k < (int)top_ports.size(); k++) {
        auto& p = top_ports[k];
        inputs.push_back(p.dir == model::direction::input ? known(0, p.width) : floating(p.width));
        int n = m_nets.port_nets[k];
        if (n >= 0 && p.dir != model::direction::output) net_states[n].port_drivers.push_back(k);
    }

    auto sch = top.get_schematic();
    instances.resize(sch ? sch->instances.size() : 0);

    for (int i = 0; i < (int)instances.size(); i++) {
        auto& inst = sch->instances[i];
        auto iface = ws.interface_of(inst);
        auto cell = ws.find_cell(inst.cell);
        // missing cells are already reported by the netlist
        if (!iface || !cell) continue;

        auto prim = ws.primitive_of(*cell);
        if (!prim) {
            m_problems.push_back("instance " + inst.name + ": '" + cell->name + "' is a schematic, simulating subcircuits is not supported yet");
            continue;
        }

        auto c = f.create(prim->kind(), model::resolve_params(prim->params(), inst.params));
        if (!c) {
            m_problems.push_back("instance " + inst.name + ": '" + cell->name + "' has no simulation model");
            continue;
        }

        auto& s = instances[i];
        s.c = std::move(c);
        s.nets = m_nets.instance_nets[i];
        for (int p = 0; p < (int)iface.ports->size(); p++) {
            auto& port = (*iface.ports)[p];
            s.widths.push_back(port.width);
            s.ports.push_back(floating(port.width));
            s.is_output.push_back(port.dir != model::direction::input);

            int n = s.nets[p];
            if (n < 0) continue;
            if (port.dir != model::direction::input) net_states[n].drivers.push_back({i, p});
            if (port.dir != model::direction::output) {
                auto& readers = net_states[n].readers;
                if (std::find(readers.begin(), readers.end(), i) == readers.end()) readers.push_back(i);
            }
        }
    }

    queued.assign(instances.size(), false);
    reset();
}

value simulator::port_value(int port) const
{
    int n = m_nets.port_nets[port];
    return n < 0 ? floating(top_ports[port].width) : net_states[n].v;
}

void simulator::set_input(int port, value v)
{
    auto m = mask(top_ports[port].width);
    inputs[port] = {v.bits & m, v.x & m, v.z & m};

    int n = m_nets.port_nets[port];
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
        net_states[n].v = floating(m_nets.nets[n].width);
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
    auto& net = m_nets.nets[n];
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
                else if (m_nets.nets[n].width_error)
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
