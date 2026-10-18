#pragma once

#include "../netlist/netlist.h"
#include "component.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace logic::sim {

// one instance of a schematic cell in the simulated hierarchy. the hierarchy
// is flattened for simulating, scopes map the cell's local nets and
// components to the flat ones, so every instance can be viewed separately.
struct scope {
    const model::cell* cell = nullptr;
    // instance name in the parent, empty for the top
    std::string name;
    // instance names from the top, separated by '/', empty for the top
    std::string path;
    const scope* parent = nullptr;

    // connectivity of the cell, shared by all scopes of the same cell
    const netlist::netlist* nets = nullptr;
    // local net -> flat net
    std::vector<int> flat_net;

    // per instance of the cell's schematic: the flat component of a
    // primitive, -1 otherwise
    std::vector<int> components;
    // per instance of the cell's schematic: index into children for
    // schematic cells, -1 otherwise
    std::vector<int> child_index;
    std::vector<std::unique_ptr<scope>> children;

    // the scope of the instance, null if it's not a schematic cell
    const scope* child(int instance) const;
};

// event driven simulation of a schematic cell and everything it contains.
// every component takes one step to react to its inputs (unit delay), a
// change is propagated until nothing changes any more.
//
// the top cell's input ports are set by the user, its output ports show the
// values of their nets.
class simulator {
public:
    // max steps of settle() before the circuit counts as oscillating
    static constexpr int max_steps = 10000;
    // max nesting of schematic cells
    static constexpr int max_depth = 64;

    // the cells must not change while the simulator exists
    simulator(const model::workspace& ws, const model::cell& top, const component_factory& f);

    // problems found while building, e.g. width mismatches or instances
    // that can't be simulated. the rest of the circuit is still simulated.
    const std::vector<std::string>& problems() const { return m_problems; }

    const scope& root() const { return *m_root; }
    // ids of all cells in the hierarchy, the top included
    const std::vector<model::id>& cells() const { return m_cells; }

    value net_value(int net) const { return net_states[net].v; }
    int net_width(int net) const { return nets[net].width; }

    // flat net of a wire / port of the scope's cell, -1 if not connected
    int net_of_wire(const scope& s, model::id wire) const;
    int net_of_port(const scope& s, int port) const;
    // z if the port is not connected
    value port_value(const scope& s, int port) const;

    // ports of the top cell. setting drives the net of an input or inout port
    value port_value(int port) const { return port_value(*m_root, port); }
    void set_input(int port, value v);
    value input_value(int port) const { return inputs[port]; }

    // advances the clocks by one tick and settles
    void tick();
    // back to the initial state, input values are kept
    void reset();

    uint64_t ticks() const { return tick_count; }
    // the last settle didn't come to rest, e.g. a gate feeding itself
    bool oscillating() const { return m_oscillating; }

private:
    struct instance {
        std::unique_ptr<component> c;
        std::vector<value> ports;
        std::vector<int> nets; // -1 if not connected
        std::vector<int> widths;
        std::vector<bool> is_output;
    };

    struct net_info {
        int width = 0;
        bool width_error = false;
    };

    struct net_state {
        value v;
        // (instance, port) driving the net
        std::vector<std::pair<int, int>> drivers;
        std::vector<int> readers;
        // top cell ports driving the net
        std::vector<int> port_drivers;
    };

    // building
    const netlist::netlist& netlist_of(const model::cell& c);
    void elaborate(scope& s, const model::cell& c, const std::vector<int>& port_nets, int depth);
    int new_net(int width, bool width_error);
    int find(int net);
    void join(int a, int b);
    void finish_nets();

    void schedule(int instance);
    // recomputes the net value, schedules the readers if it changed
    void update_net(int net);
    void settle();

    const model::workspace& ws;
    const component_factory& factory;

    std::map<const model::cell*, std::unique_ptr<netlist::netlist>> netlists;
    std::unique_ptr<scope> m_root;
    std::vector<model::id> m_cells;
    std::vector<std::string> m_problems;

    // union find over the nets while building
    std::vector<int> parent;

    std::vector<instance> instances;
    std::vector<net_info> nets;
    std::vector<net_state> net_states;

    std::vector<model::port> top_ports;
    std::vector<value> inputs;

    std::vector<int> queue;
    std::vector<bool> queued;

    uint64_t tick_count = 0;
    bool m_oscillating = false;
};

}
