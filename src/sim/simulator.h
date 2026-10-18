#pragma once

#include "../netlist/netlist.h"
#include "component.h"

#include <memory>
#include <string>
#include <vector>

namespace logic::sim {

// event driven simulation of a schematic cell. every component takes one
// step to react to its inputs (unit delay), a change is propagated until
// nothing changes any more.
//
// the cell's own input ports are set by the user, its output ports show the
// values of their nets.
class simulator {
public:
    // max steps of settle() before the circuit counts as oscillating
    static constexpr int max_steps = 10000;

    simulator(const model::workspace& ws, const model::cell& top, const component_factory& f);

    // problems found while building, e.g. width mismatches or instances
    // that can't be simulated. the rest of the circuit is still simulated.
    const std::vector<std::string>& problems() const { return m_problems; }
    const netlist::netlist& nets() const { return m_nets; }

    value net_value(int net) const { return net_states[net].v; }
    int net_width(int net) const { return m_nets.nets[net].width; }

    // value at a port of the top cell, z if the port is not connected
    value port_value(int port) const;
    // drives the net of an input or inout port of the top cell
    void set_input(int port, value v);
    value input_value(int port) const { return inputs[port]; }

    // advances the clocks by one tick and settles
    void tick();
    // back to the initial state, input values are kept
    void reset();

    uint64_t ticks() const { return tick_count; }
    // the last settle didn't come to rest, e.g. a not gate feeding itself
    bool oscillating() const { return m_oscillating; }

private:
    struct instance {
        std::unique_ptr<component> c;
        std::vector<value> ports;
        std::vector<int> nets; // -1 if not connected
        std::vector<int> widths;
        std::vector<bool> is_output;
    };

    struct net_state {
        value v;
        // (instance, port) driving the net
        std::vector<std::pair<int, int>> drivers;
        std::vector<int> readers;
        // top cell ports driving the net
        std::vector<int> port_drivers;
    };

    void schedule(int instance);
    // recomputes the net value, schedules the readers if it changed
    void update_net(int net);
    void settle();

    netlist::netlist m_nets;
    std::vector<std::string> m_problems;

    std::vector<instance> instances;
    std::vector<net_state> net_states;

    std::vector<model::port> top_ports;
    std::vector<value> inputs;

    std::vector<int> queue;
    std::vector<bool> queued;

    uint64_t tick_count = 0;
    bool m_oscillating = false;
};

}
