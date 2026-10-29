#pragma once

// the simulation helpers shared by the axi parts (axi.cpp) and other parts
// with axi ports, e.g. the risc-v cpu and devices (riscv.cpp)

#include "axi.h"
#include "builder.h"
#include "../model/primitive.h"
#include "../sim/component.h"

#include <algorithm>
#include <deque>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace logic::builtin::axi {

using model::direction;
using sim::value;

inline constexpr int okay = 0;
inline constexpr int decerr = 3;

inline bool high(const value& v, int bit)
{
    return v.get(bit) == sim::bit_state::one;
}

// a 1 bit field is 1, false if the bundle doesn't have it
inline bool high(const value& v, const sig& s)
{
    return s.w && high(v, s.off);
}

// up to 64 bits of a field of a beat or bundle value
inline uint64_t field(const value& v, int off, int w)
{
    return w ? v.slice(off, std::min(w, 64)).to_u64() : 0;
}

// a field relative to the channel's payload
inline uint64_t field(const value& beat, const channel& c, const sig& s)
{
    return field(beat, s.off - c.off, s.w);
}

inline void set(value& beat, const channel& c, const sig& s, uint64_t v)
{
    if (s.w) beat.insert(s.off - c.off, sim::known(v & sim::mask(std::min(s.w, 64)), s.w));
}

inline void set(value& beat, const channel& c, const sig& s, const value& v)
{
    if (s.w) beat.insert(s.off - c.off, v);
}

inline value zeros(int width)
{
    return sim::known(0, width);
}

// what a part drives onto a bundle port: the given channels, the other bits
// floating
struct drive {
    value v;
    explicit drive(int total)
        : v(sim::floating(total))
    {
    }
    // a channel's payload and valid, zeros while there's no beat
    void source(const channel& c, const value* beat)
    {
        if (c.w) v.insert(c.off, beat ? *beat : zeros(c.w));
        v.insert(c.valid, sim::known(beat != nullptr, 1));
    }
    void sink(const channel& c, bool ready) { v.insert(c.ready, sim::known(ready, 1)); }
};

// a skid buffer of two beats: ready while it has room, valid while it
// isn't empty
struct fifo {
    std::deque<value> q;
    bool ready() const { return q.size() < 2; }
    const value* head() const { return q.empty() ? nullptr : &q.front(); }
    bool empty() const { return q.empty(); }
    value pop()
    {
        auto v = std::move(q.front());
        q.pop_front();
        return v;
    }
};

// clock edges and reset of the axi parts
class clocked : public sim::component {
public:
    clocked(int clk, int rst)
        : clk_port(clk)
        , rst_port(rst)
    {
    }

    void eval(std::span<value> ports, sim::context& ctx) override
    {
        auto clk = ports[clk_port].get(0);
        // while the clock was ignored its edges weren't seen: the value is
        // only taken up, an edge at this time came before what woke the part
        bool rising = listening && prev_clk == sim::bit_state::zero && clk == sim::bit_state::one;
        prev_clk = clk;
        if (rising) {
            // not connected is not in reset
            if (ports[rst_port].get(0) == sim::bit_state::zero)
                clear();
            else
                edge(ports);
            dirty = true;
        }
        // a part with nothing to do and nothing offered ignores its clock
        // until a handshake or reset input changes. before outputs, which
        // overwrite the inouts' values.
        bool idle_now = ports[rst_port].get(0) != sim::bit_state::zero && idle(ports);
        // the outputs come from the state only, they're made again when it
        // may have changed
        if (dirty || cache.size() != ports.size()) {
            outputs(ports);
            cache.assign(ports.begin(), ports.end());
            dirty = false;
        } else {
            std::copy(cache.begin(), cache.end(), ports.begin());
        }
        if (idle_now == listening) {
            listening = !idle_now;
            ctx.set_sensitive(clk_port, listening);
        }
    }

    void reset() override
    {
        prev_clk = sim::bit_state::z;
        listening = true;
        dirty = true;
        clear();
    }

protected:
    // the state changed other than at an edge, e.g. from interact: the
    // outputs are made again
    void changed() { dirty = true; }

    // the state after reset
    virtual void clear() = 0;
    // the handshakes seen at a rising edge of aclk
    virtual void edge(std::span<const value> ports) = 0;
    // from the state only, there are no combinational paths
    virtual void outputs(std::span<value> ports) = 0;
    // nothing to do at the next edge: no state that moves and no valid
    // offered to the part, with the ports as they are now
    virtual bool idle(std::span<const value> ports) const
    {
        (void)ports;
        return false;
    }

private:
    int clk_port, rst_port;
    sim::bit_state prev_clk = sim::bit_state::z;
    // the clock is sensitive
    bool listening = true;
    bool dirty = true;
    // the ports as outputs left them
    std::vector<value> cache;
};

inline const char* resp_name(int r)
{
    static const char* const names[] = {"OKAY", "EXOKAY", "SLVERR", "DECERR"};
    return names[r & 3];
}

// the address of beat k of a burst
inline uint64_t beat_address(uint64_t addr, int size, int burst, int len, int k)
{
    uint64_t bytes = uint64_t(1) << size;
    uint64_t aligned = addr & ~(bytes - 1);
    switch (burst) {
    case 0: return addr; // fixed
    case 2: {
        // wrap at the burst's size, the start is aligned
        uint64_t total = bytes * uint64_t(len + 1);
        uint64_t base = aligned / total * total;
        return base + (aligned - base + uint64_t(k) * bytes) % total;
    }
    default: return k == 0 ? addr : aligned + uint64_t(k) * bytes;
    }
}

// a bundle port of an axi part for its parameters
inline void bundle_port(model::port& p, protocol proto, const widths& w, bool flipped)
{
    p.bundle = model::builtin_bundle_id(bundle_name(proto));
    p.flipped = flipped;
    p.bundle_args = bundle_args(proto, w);
}

// a problem with the widths, empty if there is none
inline std::string check_widths(protocol proto, const widths& w)
{
    if (w.data < 8 || (w.data & (w.data - 1))) return "the data bits must be a power of two, at least 8";
    if (proto == protocol::lite && w.data != 32 && w.data != 64) return "AXI4-Lite has 32 or 64 data bits";
    return {};
}

// the parts' widths as they can be built: lite's data bits clamped
inline widths usable(protocol proto, widths w)
{
    if (proto == protocol::lite) w.data = w.data <= 32 ? 32 : 64;
    while (w.data & (w.data - 1))
        w.data &= w.data - 1;
    return w;
}

inline model::cell_interface finish_ports(builder& b, protocol proto, const widths& w, const std::vector<std::pair<std::string, bool>>& bundles)
{
    auto r = b.finish();
    for (auto& p : r.ports)
        for (auto& [name, flipped] : bundles)
            if (p.name == name) bundle_port(p, proto, w, flipped);
    return r;
}

inline builder::box_pin clock_pin()
{
    return {"aclk", direction::input, 1, "clock, everything happens on its rising edge"};
}

inline builder::box_pin reset_pin()
{
    return {"aresetn", direction::input, 1, "0 resets at the rising edge of aclk, not connected: running"};
}

}

