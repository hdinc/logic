#pragma once

#include "../netlist/netlist.h"
#include "component.h"
#include "waveform.h"

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

// the changing part of a simulation: everything needed to show its values.
// copied out of the simulator for drawing while it keeps running, see
// snapshot.
struct state {
    // values of the nodes, see simulator
    std::vector<value> nodes;
    // values set for the top cell's ports
    std::vector<value> inputs;
    time now = 0;
    // the last settle didn't come to rest, e.g. a gate feeding itself
    bool oscillating = false;
    // why a component paused the simulation (e.g. a breakpoint), empty if
    // it didn't. cleared when it goes on.
    std::string paused;
};

// event driven simulation of a schematic cell and everything it contains.
//
// time advances from event to event. events are wakes requested by
// components (e.g. a clock's next edge). at one point in time every
// component takes one step to react to its inputs (unit delay, like delta
// cycles), a change is propagated until nothing changes any more. splitters
// are wiring: the bits they join are one signal.
//
// the top cell's input ports are set by the user, its output ports show the
// values of their nets.
class simulator {
public:
    waveform_recorder waveforms;

    // max steps at one point in time before the circuit counts as oscillating
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

    int net_width(int net) const { return nets[net].width; }
    // flat net of a wire / port of the scope's cell, -1 if not connected
    int net_of_wire(const scope& s, model::id wire) const;
    int net_of_port(const scope& s, int port) const;

    // values in a state of this simulator, e.g. a copy taken earlier
    value net_value(const state& st, int net) const;
    // z if the port is not connected
    value port_value(const state& st, const scope& s, int port) const;

    // the same for the current state
    const state& current() const { return cur; }
    value net_value(int net) const { return net_value(cur, net); }
    value port_value(const scope& s, int port) const { return port_value(cur, s, port); }

    // ports of the top cell. setting drives the net of an input or inout
    // port, z releases an inout.
    value port_value(int port) const { return port_value(*m_root, port); }
    void set_input(int port, value v);
    value input_value(int port) const { return cur.inputs[port]; }

    time now() const { return cur.now; }
    // time of the next pending wake, never if there is none
    time next_event() const;
    // the earliest pending wake after t, or the time run_until is going to
    // if that's earlier
    time event_after(time t) const;
    // processes all events up to and including t, then the time is t.
    // returns false if it stopped early because max_events points in time
    // were processed, the time is then the last of them.
    bool run_until(time t, int max_events = std::numeric_limits<int>::max());
    // processes what's due now, then advances to the next event and
    // processes it. false if there is none.
    bool step();
    // back to time 0 and the initial state, input values are kept
    void reset();

    bool oscillating() const { return cur.oscillating; }

    // the state of the component of an instance in the scope's schematic,
    // empty if it has none
    std::vector<inspect_item> inspect(const scope& s, int instance) const;

    // the component of an instance in the scope's schematic, null if it has
    // none. for debugging views.
    component* component_of(const scope& s, int instance);
    // passes a user action to the instance's component and settles, false
    // if it has no component
    bool interact(const scope& s, int instance, const std::string& action, int64_t value);
    // the reason a component paused the simulation, see state::paused
    const std::string& paused() const { return cur.paused; }
    // forgets the pause, so that running loops that stop at a pause go on.
    // run_until and step do this themselves.
    void resume() { cur.paused.clear(); }

private:
    enum class port_kind : uint8_t {
        in,
        out,
        inout
    };

    struct instance {
        std::unique_ptr<component> c;
        // what eval sees and writes
        std::vector<value> ports;
        // what the outputs and inouts drive onto their nets
        std::vector<value> drive;
        std::vector<int> nets; // -1 if not connected
        std::vector<int> widths;
        std::vector<port_kind> kinds;
        // requested wake, never if none
        time wake = never;
        // the wake is due, eval sees it in the context
        bool woken = false;
        // instance names from the top, for messages
        std::string path;
    };

    // bits [net_bit, net_bit + width) of a net are bits [node_bit, ...) of
    // a node
    struct slice {
        int node;
        int node_bit;
        int net_bit;
        int width;
    };

    struct net_info {
        int width = 0;
        bool width_error = false;
        // a net is one node unless splitters join some of its bits to other nets
        std::vector<slice> slices;
    };

    // bits of another net joined by a splitter, while building
    struct alias {
        int a, a_bit, b, b_bit, width;
    };

    // a pull resistor on a net, while building
    struct net_pull {
        int net;
        bool up;
    };

    // something driving bits of a node: an output of an instance (instance,
    // port) or an input of the top cell (-1, port)
    struct driver {
        int instance;
        int port;
        int src_bit; // first bit in the driving port's value
        int dst_bit; // first bit in the node
        int width;
    };

    // a set of bits that always have the same value, the value is resolved
    // from all its drivers. the values are in state::nodes.
    struct node {
        int width = 0;
        bool width_error = false;
        // weak values for bits nothing drives
        bool pull_up = false;
        bool pull_down = false;
        std::vector<driver> drivers;
        std::vector<int> readers;
    };

    // a pending wake in the event queue. stale when the instance's wake
    // changed since.
    struct event {
        time t;
        int instance;
        bool operator>(const event& o) const { return t > o.t; }
    };

    // building
    const netlist::netlist& netlist_of(const model::cell& c);
    void elaborate(scope& s, const model::cell& c, const std::vector<int>& port_nets, int depth);
    // the component of instance `index` of the scope's schematic
    void add_instance(scope& s, int index, std::unique_ptr<component> c, const std::vector<model::port>& ports, const std::vector<int>& pins,
        std::string path);
    int new_net(int width, bool width_error);
    int find(int net);
    void join(int a, int b);
    void finish_nets();
    // splits nets into nodes, joining the aliased bits
    void build_nodes();
    void add_driver(int net, int instance, int port);

    void schedule(int instance);
    void set_wake(int instance, time t);
    // drops stale events from the front of the queue
    void clean_events();
    // recomputes the values of the net's nodes, schedules the readers of
    // those that changed
    void update_net(int net);
    void update_node(int node);
    // runs the scheduled instances until nothing changes, at the current time
    void settle();
    // run_until without setting the target
    bool run(time t, int max_events);

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
    std::vector<alias> aliases;
    std::vector<net_pull> pulls;
    std::vector<node> nodes;

    std::vector<model::port> top_ports;

    state cur;

    // instances to evaluate in the next step
    std::vector<int> queue;
    std::vector<bool> queued;
    // min heap of wakes
    std::vector<event> events;
    // where run_until is going, never outside of it
    time target = never;

    // reused by update_node
    std::vector<value> scratch;
};

// values of a simulation at one point in time with the queries of the
// simulator. the simulator's structure (scopes, nets) doesn't change, a
// snapshot only copies the values.
class snapshot {
public:
    explicit snapshot(const simulator& s)
        : s(&s)
        , st(s.current())
    {
    }

    const simulator& sim() const { return *s; }
    const state& values() const { return st; }
    void take() { st = s->current(); }
    void take(const state& from) { st = from; }

    const std::vector<std::string>& problems() const { return s->problems(); }
    const scope& root() const { return s->root(); }

    int net_width(int net) const { return s->net_width(net); }
    int net_of_wire(const scope& sc, model::id wire) const { return s->net_of_wire(sc, wire); }
    int net_of_port(const scope& sc, int port) const { return s->net_of_port(sc, port); }

    value net_value(int net) const { return s->net_value(st, net); }
    value port_value(const scope& sc, int port) const { return s->port_value(st, sc, port); }
    value port_value(int port) const { return port_value(s->root(), port); }
    value input_value(int port) const { return st.inputs[port]; }

    time now() const { return st.now; }
    bool oscillating() const { return st.oscillating; }
    const std::string& paused() const { return st.paused; }

private:
    const simulator* s;
    state st;
};

}
