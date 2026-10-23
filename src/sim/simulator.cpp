#include "simulator.h"

#include <algorithm>
#include <functional>

namespace logic::sim {

std::string format_time(time t)
{
    return wave::format_time(t, {-12});
}

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
    build_nodes();

    cur.nodes.resize(nodes.size());

    for (int k = 0; k < (int)top_ports.size(); k++) {
        auto& p = top_ports[k];
        int n = net_of_port(*m_root, k);
        auto layout = p.bundle != model::no_id ? ws.layout_of(p.bundle, p.flipped) : model::bundle_layout{};
        if (layout && layout.width == p.width) {
            // the outside drives the fields that are inputs of the cell,
            // inouts start released
            value v = floating(p.width);
            for (auto& l : layout.leaves) {
                if (l.dir == model::direction::output) continue;
                if (l.dir == model::direction::input) v.insert(l.offset, known(0, l.width));
                if (n >= 0) add_driver(n, -1, k, l.offset, l.width);
            }
            cur.inputs.push_back(std::move(v));
            continue;
        }
        cur.inputs.push_back(p.dir == model::direction::input ? known(0, p.width) : floating(p.width));
        if (n >= 0 && p.dir != model::direction::output) add_driver(n, -1, k);
    }

    for (int i = 0; i < (int)instances.size(); i++) {
        auto& s = instances[i];
        for (int p = 0; p < (int)s.nets.size(); p++) {
            int n = s.nets[p];
            if (n < 0) continue;
            if (s.kinds[p] != port_kind::in) add_driver(n, i, p);
            if (s.kinds[p] == port_kind::out) continue;
            for (auto& sl : nets[n].slices) {
                auto& readers = nodes[sl.node].readers;
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

    // relative file parameters of the instances start at this cell's library
    auto lib = ws.library_of(c.id);
    auto dir = lib ? lib->dir : std::filesystem::path{};

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
            // bits joined inside the primitive. an unconnected pin still
            // joins the others, e.g. two ends of a splitter without a bus.
            auto& links = *iface.links;
            for (auto& l : links) {
                for (int p : {l.a, l.b})
                    if (pins[p] < 0) pins[p] = new_net((*iface.ports)[p].width, false);
                aliases.push_back({pins[l.a], l.a_bit, pins[l.b], l.b_bit, l.width});
            }
            // a pull on a pin without a wire has nothing to pull
            for (auto& p : *iface.pulls)
                if (pins[p.port] >= 0) pulls.push_back({pins[p.port], p.up});

            std::string error;
            auto values = model::resolve_params(prim->params(), inst.params);
            auto comp = factory.create({*cell, values, dir}, error);
            if (comp)
                add_instance(s, i, std::move(comp), *iface.ports, pins, s.path.empty() ? inst.name : s.path + "/" + inst.name);
            else if (!error.empty())
                m_problems.push_back(where(inst) + ": " + error);
            else if (!prim->is_wiring())
                m_problems.push_back(where(inst) + ": '" + cell->name + "' has no simulation model");
        } else if (auto defs = cell->external_params()) {
            auto groups = cell->port_groups();
            auto& model_ports = *iface.model_ports;
            if (groups && !groups->empty() && model_ports.empty()) {
                m_problems.push_back(where(inst) + ": the bundle ports of '" + cell->name + "' don't fit its model, refresh its interface");
                continue;
            }

            // the model's ports are bits of the instance's ports, e.g. the
            // fields of a bundle port: their own nets joined to those bits
            const std::vector<model::port>* ports = iface.ports;
            std::vector<model::port> leaves;
            if (!model_ports.empty()) {
                std::vector<int> leaf_pins;
                for (auto& m : model_ports) {
                    leaves.push_back(m.port);
                    int net = -1;
                    if (pins[m.of] >= 0) {
                        net = new_net(m.port.width, false);
                        aliases.push_back({net, 0, pins[m.of], m.bit, m.port.width});
                    }
                    leaf_pins.push_back(net);
                }
                ports = &leaves;
                pins = std::move(leaf_pins);
            }

            std::string error;
            auto values = model::resolve_params(*defs, inst.params);
            auto comp = factory.create({*cell, values, dir}, error);
            if (comp)
                add_instance(s, i, std::move(comp), *ports, pins, s.path.empty() ? inst.name : s.path + "/" + inst.name);
            else
                m_problems.push_back(where(inst) + ": " + (error.empty() ? "nothing can simulate '" + cell->name + "'" : error));
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

void simulator::add_instance(scope& s, int index, std::unique_ptr<component> c, const std::vector<model::port>& ports, const std::vector<int>& pins,
    std::string path)
{
    instance x;
    x.c = std::move(c);
    x.path = std::move(path);
    x.nets = pins;
    for (auto& port : ports) {
        x.widths.push_back(port.width);
        x.ports.push_back(floating(port.width));
        x.drive.push_back(floating(port.width));
        x.kinds.push_back(port.dir == model::direction::input ? port_kind::in
                : port.dir == model::direction::output    ? port_kind::out
                                                          : port_kind::inout);
    }
    s.components[index] = (int)instances.size();
    instances.push_back(std::move(x));
}

int simulator::new_net(int width, bool width_error)
{
    parent.push_back((int)parent.size());
    auto& n = nets.emplace_back();
    n.width = width;
    n.width_error = width_error;
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
    for (auto& a : aliases) {
        a.a = index[a.a];
        a.b = index[a.b];
    }
    for (auto& p : pulls)
        p.net = index[p.net];

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

void simulator::build_nodes()
{
    // every bit of a net touched by a splitter gets its own place in a union
    // find, the joined bits become one node. nets with a width error are
    // not joined, they are unknown anyway.
    std::vector<int> base(nets.size(), -1);
    int bits = 0;
    for (auto& a : aliases) {
        for (int n : {a.a, a.b}) {
            if (base[n] >= 0 || nets[n].width_error || nets[n].width == 0) continue;
            base[n] = bits;
            bits += nets[n].width;
        }
    }

    parent.resize(bits);
    for (int b = 0; b < bits; b++)
        parent[b] = b;
    for (auto& a : aliases) {
        if (base[a.a] < 0 || base[a.b] < 0) continue;
        for (int k = 0; k < a.width; k++) {
            int x = a.a_bit + k, y = a.b_bit + k;
            if (x >= nets[a.a].width || y >= nets[a.b].width) break;
            parent[find(base[a.a] + x)] = find(base[a.b] + y);
        }
    }

    std::vector<int> bit_node(bits, -1);
    for (int n = 0; n < (int)nets.size(); n++) {
        auto& net = nets[n];
        if (base[n] < 0) {
            net.slices = {{(int)nodes.size(), 0, 0, net.width}};
            auto& nd = nodes.emplace_back();
            nd.width = net.width;
            nd.width_error = net.width_error;
            continue;
        }
        for (int b = 0; b < net.width; b++) {
            int r = find(base[n] + b);
            if (bit_node[r] < 0) {
                bit_node[r] = (int)nodes.size();
                nodes.emplace_back().width = 1;
            }
            net.slices.push_back({bit_node[r], 0, b, 1});
        }
    }

    for (auto& p : pulls) {
        for (auto& sl : nets[p.net].slices) {
            auto& nd = nodes[sl.node];
            (p.up ? nd.pull_up : nd.pull_down) = true;
        }
    }

    parent.clear();
    aliases.clear();
    pulls.clear();
}

void simulator::add_driver(int net, int instance, int port)
{
    for (auto& sl : nets[net].slices)
        nodes[sl.node].drivers.push_back({instance, port, sl.net_bit, sl.node_bit, sl.width});
}

void simulator::add_driver(int net, int instance, int port, int lo, int width)
{
    for (auto& sl : nets[net].slices) {
        int a = std::max(lo, sl.net_bit), b = std::min(lo + width, sl.net_bit + sl.width);
        if (a < b) nodes[sl.node].drivers.push_back({instance, port, a, sl.node_bit + a - sl.net_bit, b - a});
    }
}

value simulator::net_value(const state& st, int n) const
{
    auto& net = nets[n];
    if (net.width_error) return unknown(net.width);
    // a whole node
    if (net.slices.size() == 1) return st.nodes[net.slices[0].node];

    value v = floating(net.width);
    for (auto& sl : net.slices)
        v.insert(sl.net_bit, st.nodes[sl.node].slice(sl.node_bit, sl.width));
    return v;
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

value simulator::port_value(const state& st, const scope& s, int port) const
{
    int n = net_of_port(s, port);
    return n < 0 ? floating(s.cell->ports[port].width) : net_value(st, n);
}

void simulator::set_input(int port, value v)
{
    int w = top_ports[port].width;
    cur.inputs[port] = v.width() == w ? std::move(v) : v.slice(0, w);

    int n = net_of_port(*m_root, port);
    if (n >= 0) update_net(n);
    settle();
}

component* simulator::component_of(const scope& s, int instance)
{
    if (instance < 0 || instance >= (int)s.components.size()) return nullptr;
    int k = s.components[instance];
    return k < 0 ? nullptr : instances[k].c.get();
}

bool simulator::interact(const scope& s, int instance, const std::string& action, int64_t value)
{
    if (instance < 0 || instance >= (int)s.components.size() || s.components[instance] < 0) return false;
    int k = s.components[instance];
    if (instances[k].c->interact(action, value)) schedule(k);
    settle();
    return true;
}

std::vector<inspect_item> simulator::inspect(const scope& s, int instance) const
{
    std::vector<inspect_item> r;
    if (instance < 0 || instance >= (int)s.components.size()) return r;
    int k = s.components[instance];
    if (k >= 0) instances[k].c->inspect(r);
    return r;
}

void simulator::set_wake(int i, time t)
{
    auto& s = instances[i];
    if (t == never) {
        // the queued event goes stale
        s.wake = never;
        return;
    }
    s.wake = std::max(t, cur.now);
    events.push_back({s.wake, i});
    std::push_heap(events.begin(), events.end(), std::greater<>{});
}

void simulator::clean_events()
{
    while (!events.empty() && instances[events.front().instance].wake != events.front().t) {
        std::pop_heap(events.begin(), events.end(), std::greater<>{});
        events.pop_back();
    }
}

time simulator::next_event() const
{
    // the earliest event that is still wanted. stale ones are only dropped
    // by run_until, look past them.
    if (!events.empty() && instances[events.front().instance].wake == events.front().t) return events.front().t;
    time t = never;
    for (auto& e : events)
        if (instances[e.instance].wake == e.t) t = std::min(t, e.t);
    return t;
}

time simulator::event_after(time t) const
{
    // the time run_until goes to counts too: nothing runs ahead of it, so
    // inputs set after it are in time. it may be t itself.
    time r = target != never && target >= t ? target : never;
    for (auto& e : events)
        if (e.t > t && instances[e.instance].wake == e.t) r = std::min(r, e.t);
    return r;
}

bool simulator::run_until(time t, int max_events)
{
    target = t;
    struct clear_target {
        time& t;
        ~clear_target() { t = never; }
    } clear{target};
    return run(t, max_events);
}

bool simulator::run(time t, int max_events)
{
    // going on after a pause
    cur.paused.clear();
    settle();
    if (cur.oscillating || !cur.paused.empty()) return false;

    int n = 0;
    // rounds of wakes at the same time, a component that keeps asking for
    // the current time would never let time advance
    int same_time = 0;
    time last = cur.now;

    while (true) {
        clean_events();
        if (events.empty() || events.front().t > t) break;
        if (n == max_events) return false;

        time at = std::max(events.front().t, cur.now);
        if (at == last) {
            if (++same_time > max_steps) {
                cur.oscillating = true;
                return false;
            }
        } else {
            same_time = 0;
            last = at;
            n++;
        }
        cur.now = at;

        while (!events.empty() && events.front().t <= cur.now) {
            auto e = events.front();
            std::pop_heap(events.begin(), events.end(), std::greater<>{});
            events.pop_back();
            auto& s = instances[e.instance];
            if (s.wake != e.t) continue;
            s.wake = never;
            s.woken = true;
            schedule(e.instance);
        }

        settle();
        if (cur.oscillating || !cur.paused.empty()) return false;
    }

    if (t > cur.now) cur.now = t;
    waveforms.capture(*this);
    return true;
}

bool simulator::step()
{
    time t = next_event();
    if (t == never) return false;
    // what's due now first (e.g. an external simulator running ahead), then
    // the next point in time
    if (t <= cur.now) {
        if (!run(cur.now, std::numeric_limits<int>::max()) && !cur.paused.empty()) return true;
        t = next_event();
        if (t == never) return true;
    }
    // without a target: what runs ahead may go to the next event
    run(t, std::numeric_limits<int>::max());
    return true;
}

void simulator::reset()
{
    cur.now = 0;
    waveforms.clear(*this);
    cur.oscillating = false;
    cur.paused.clear();
    events.clear();

    for (int i = 0; i < (int)instances.size(); i++) {
        auto& s = instances[i];
        s.c->reset();
        for (size_t p = 0; p < s.ports.size(); p++) {
            s.ports[p] = floating(s.widths[p]);
            s.drive[p] = floating(s.widths[p]);
        }
        s.wake = never;
        s.woken = false;
        schedule(i);
    }

    for (int n = 0; n < (int)nodes.size(); n++) {
        cur.nodes[n] = floating(nodes[n].width);
        update_node(n);
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
    for (auto& sl : nets[n].slices)
        update_node(sl.node);
}

void simulator::update_node(int n)
{
    auto& nd = nodes[n];

    value v;
    if (nd.width_error) {
        v = unknown(nd.width);
    } else {
        // each driver drives its bits, the others are z for it
        scratch.clear();
        for (auto& d : nd.drivers) {
            auto& src = d.instance < 0 ? cur.inputs[d.port] : instances[d.instance].drive[d.port];
            if (d.src_bit == 0 && d.dst_bit == 0 && d.width == nd.width && src.width() == nd.width) {
                scratch.push_back(src);
            } else {
                auto& full = scratch.emplace_back(floating(nd.width));
                full.insert(d.dst_bit, src.slice(d.src_bit, d.width));
            }
        }
        v = resolve(scratch, nd.width);

        // pulled bits that nothing drives, both directions at once is a conflict
        if (nd.pull_up || nd.pull_down) {
            for (int w = 0; w < v.words(); w++) {
                auto z = v.z(w);
                if (!z) continue;
                bool both = nd.pull_up && nd.pull_down;
                v.set_word(w, v.bits(w) | (nd.pull_up && !both ? z : 0), v.x(w) | (both ? z : 0), 0);
            }
        }
    }

    if (v == cur.nodes[n]) return;
    cur.nodes[n] = std::move(v);
    for (int r : nd.readers)
        schedule(r);
}

void simulator::settle()
{
    cur.oscillating = false;

    std::vector<int> batch;
    std::vector<int> changed;

    for (int step = 0; !queue.empty(); step++) {
        if (step == max_steps) {
            cur.oscillating = true;
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
            // outputs see what they drive, inputs and inouts their net. an
            // inout without a net sees what it drives.
            for (size_t p = 0; p < s.ports.size(); p++) {
                int n = s.nets[p];
                if (s.kinds[p] == port_kind::out || (n < 0 && s.kinds[p] == port_kind::inout))
                    s.ports[p] = s.drive[p];
                else if (n < 0)
                    s.ports[p] = floating(s.widths[p]);
                else if (nets[n].width_error)
                    s.ports[p] = unknown(s.widths[p]);
                else
                    s.ports[p] = net_value(n);
            }

            context ctx(cur.now, s.woken, s.wake, [](const void* self, time now) { return ((const simulator*)self)->event_after(now); }, this);
            s.woken = false;
            s.c->eval(s.ports, ctx);
            if (ctx.wake() != s.wake) set_wake(i, ctx.wake());
            if (!ctx.pause_request().empty()) cur.paused = s.path + ": " + ctx.pause_request();

            for (size_t p = 0; p < s.ports.size(); p++) {
                if (s.kinds[p] == port_kind::in) continue;
                auto& v = s.ports[p];
                // a component writing the wrong width is a bug, make it visible
                if (v.width() != s.widths[p]) v = unknown(s.widths[p]);
                if (v == s.drive[p]) continue;
                s.drive[p] = v;
                if (s.nets[p] >= 0)
                    changed.push_back(s.nets[p]);
                else if (s.kinds[p] == port_kind::inout)
                    schedule(i); // it reads what it drives
            }
        }

        for (int n : changed)
            update_net(n);
    }
    waveforms.capture(*this);
}

}
