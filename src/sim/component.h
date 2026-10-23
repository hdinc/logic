#pragma once

#include "../model/param.h"
#include "value.h"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace logic::model {
struct cell;
}

namespace logic::sim {

// simulated time in picoseconds
using time = uint64_t;
constexpr time never = std::numeric_limits<time>::max();

constexpr time ps = 1;
constexpr time ns = 1000 * ps;
constexpr time us = 1000 * ns;
constexpr time ms = 1000 * us;
constexpr time seconds = 1000 * ms;

// e.g. "0 s", "12.5 us", "1.000002 s"
std::string format_time(time t);

// what a component sees while it is evaluated
class context {
public:
    // horizon(owner, now) finds the next event after now, see horizon()
    using horizon_fn = time (*)(const void* owner, time now);

    explicit context(time now, bool woken, time wake, horizon_fn h = nullptr, const void* owner = nullptr)
        : m_now(now)
        , m_woken(woken)
        , m_wake(wake)
        , m_horizon(h)
        , m_owner(owner)
    {
    }

    time now() const { return m_now; }
    // the time asked for with wake_at has come. the inputs may have changed
    // at the same time.
    bool woken() const { return m_woken; }

    // eval is called again at t, even if no input changes. replaces an
    // earlier request, `never` cancels it. a time that is not in the future
    // wakes after the current step.
    void wake_at(time t) { m_wake = t; }
    void wake_after(time dt) { m_wake = m_now + dt; }
    // the pending request, never if there is none
    time wake() const { return m_wake; }

    // the earliest time after now at which something else in the
    // simulation is due, or the time the simulation is run to if that's
    // earlier (which may be now itself), never if nothing is. until then the
    // inputs can only change through this component's own outputs. for
    // models that run ahead, e.g. an external simulator.
    time horizon() const { return m_horizon ? m_horizon(m_owner, m_now) : never; }

    // asks to pause the simulation after the current point in time, e.g. at
    // a breakpoint. the reason is shown to the user.
    void request_pause(std::string reason) { m_pause = std::move(reason); }
    const std::string& pause_request() const { return m_pause; }

private:
    time m_now;
    bool m_woken;
    time m_wake;
    horizon_fn m_horizon;
    const void* m_owner;
    std::string m_pause;
};

// a named part of a component's state, shown by debugging views: a value,
// or a text (e.g. the screen of a terminal) if the value has width 0
struct inspect_item {
    std::string name;
    value v;
    std::string text;
};

struct memory_space {
    std::string name;
    // in bytes
    uint64_t size = 0;
};

// memories and execution of a processor or memory part, for debugging views.
// called on the simulation thread only.
class debug_interface {
public:
    virtual ~debug_interface() = default;

    virtual std::vector<memory_space> memories() const { return {}; }
    // false if the range isn't in the memory
    virtual bool read_memory(int space, uint64_t address, std::span<uint8_t> out) const
    {
        (void)space, (void)address, (void)out;
        return false;
    }
    virtual bool write_memory(int space, uint64_t address, std::span<const uint8_t> data)
    {
        (void)space, (void)address, (void)data;
        return false;
    }

    // the program counter of a processor
    virtual std::optional<uint64_t> pc() const { return std::nullopt; }
    // the instruction at the address as text and its length in bytes, 0 if
    // it can't be disassembled
    virtual int disassemble(uint64_t address, std::string& text) const
    {
        (void)address, (void)text;
        return 0;
    }
    // the processor pauses the simulation before executing there
    virtual void set_breakpoint(uint64_t address, bool on) { (void)address, (void)on; }
    virtual std::vector<uint64_t> breakpoints() const { return {}; }
    // pauses the simulation after the next instruction
    virtual void step_instruction() {}
};

// behaviour of one instance of a primitive. ports are in the order of the
// primitive's interface ports.
class component {
public:
    virtual ~component() = default;

    // called when an input changed, when a wake requested from the context is
    // due, and once after reset. on entry inputs and inouts hold the values
    // of their nets, outputs what was written last. it must set outputs and
    // inouts to what the component drives, z for bits it doesn't drive. the
    // values are compared afterwards to find changes.
    virtual void eval(std::span<value> ports, context& ctx) = 0;

    // back to the state after creation. pending wakes are cancelled by the
    // simulator.
    virtual void reset() {}

    // the internal state, e.g. register contents, for debugging views
    virtual void inspect(std::vector<inspect_item>&) const {}

    // memories and execution for the debugger, null if there is nothing
    virtual debug_interface* debug() { return nullptr; }

    // a user action while simulating, e.g. "press" and "release" on a
    // button or "key" with a character on a terminal. true if the outputs
    // may change, eval is called then.
    virtual bool interact(const std::string& action, int64_t value)
    {
        (void)action, (void)value;
        return false;
    }

    // what a display shows, averaged over time: e.g. the fraction of the
    // simulated time each segment was lit since the last call, which starts
    // a new window. only called for components with has_view(), on the
    // simulation thread when values are published.
    virtual bool has_view() const { return false; }
    virtual void sample_view(time now, std::vector<float>& out) { (void)now, (void)out; }

    // copies the internal state into bytes and back, for snapshots of the
    // whole simulation. false if the component doesn't support it.
    virtual bool save_state(std::vector<uint8_t>&) const { return false; }
    virtual bool load_state(std::span<const uint8_t>) { return false; }
};

// what a factory needs to create the model of an instance
struct create_info {
    const model::cell& cell;
    // resolved
    const model::param_values& params;
    // relative file parameters (e.g. firmware) start here: the folder of
    // the library with the schematic the instance is in
    std::filesystem::path dir;
};

// creates the simulation models of cells that aren't schematics
class component_factory {
public:
    virtual ~component_factory() = default;

    // null if the factory doesn't handle the cell, or with an error if it
    // does but can't create the model
    virtual std::unique_ptr<component> create(const create_info& info, std::string& error) const = 0;
};

// asks factories in order until one creates the model or reports an error
class factory_chain : public component_factory {
public:
    explicit factory_chain(std::vector<const component_factory*> list)
        : list(std::move(list))
    {
    }

    std::unique_ptr<component> create(const create_info& info, std::string& error) const override
    {
        for (auto f : list) {
            auto r = f->create(info, error);
            if (r || !error.empty()) return r;
        }
        return nullptr;
    }

private:
    std::vector<const component_factory*> list;
};

}
