#pragma once

#include "../model/param.h"
#include "value.h"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
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
    // listened(owner, instance, port), see listened()
    using listened_fn = bool (*)(const void* owner, int instance, int port);

    explicit context(time now, bool woken, time wake, horizon_fn h = nullptr, const void* owner = nullptr, listened_fn l = nullptr, int instance = -1)
        : m_now(now)
        , m_woken(woken)
        , m_wake(wake)
        , m_horizon(h)
        , m_owner(owner)
        , m_listened(l)
        , m_instance(instance)
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

    // off: changes of the input or inout port don't call eval any more, e.g.
    // the clock of a part that has nothing to do. eval still sees the port's
    // current value when it's called for other reasons. on again with true.
    // all ports are on after reset.
    void set_sensitive(int port, bool on) { m_sensitivity.push_back({port, on}); }
    const std::vector<std::pair<int, bool>>& sensitivity_requests() const { return m_sensitivity; }

    // whether changes of an output's net call anyone's eval or are
    // recorded: false if every reader ignores it (see set_sensitive), e.g.
    // a clock whose parts are idle may stop then. true without a simulator.
    bool listened(int port) const { return !m_listened || m_listened(m_owner, m_instance, port); }
    // eval is called when a reader of one of the outputs' nets becomes
    // sensitive again, or the net starts being recorded
    void notify_on_listen(bool on) { m_notify = on ? 1 : 0; }
    int notify_request() const { return m_notify; }

private:
    time m_now;
    bool m_woken;
    time m_wake;
    horizon_fn m_horizon;
    const void* m_owner;
    listened_fn m_listened;
    int m_instance;
    std::string m_pause;
    std::vector<std::pair<int, bool>> m_sensitivity;
    // -1 unchanged
    int m_notify = -1;
};

// a named part of a component's state, shown by debugging views: a value,
// or a text (e.g. the screen of a terminal) if the value has width 0
struct inspect_item {
    std::string name;
    value v;
    std::string text;
};

// the picture of a display part, e.g. a framebuffer. the component writes it
// on the simulation thread, the ui copies it out while drawing: both hold m
// while touching rgba or version. version goes up with every change, so
// the ui only copies after one.
struct pixel_buffer {
    pixel_buffer(int width, int height)
        : width(width)
        , height(height)
        , rgba(size_t(width) * height, 0xFF000000u)
    {
    }

    const int width;
    const int height;
    std::mutex m;
    // row by row from the top left, red in the low byte, alpha 255
    std::vector<uint32_t> rgba;
    uint64_t version = 0;
};

// what a terminal part sends, e.g. a uart: bytes for a terminal window. the
// component appends on the simulation thread, the ui takes them while
// drawing: both hold m. resets goes up when the component is reset, the
// window clears its screen then.
struct byte_stream {
    std::mutex m;
    std::string bytes;
    uint64_t resets = 0;
    // bytes the ui hasn't taken are dropped from the front beyond this
    static constexpr size_t max_pending = size_t(1) << 22;

    void append(char c)
    {
        std::lock_guard lk(m);
        if (bytes.size() >= max_pending) bytes.erase(0, max_pending / 2);
        bytes += c;
    }
    void reset()
    {
        std::lock_guard lk(m);
        bytes.clear();
        resets++;
    }
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

    // the picture of a display part, null for other components. the same
    // buffer for the component's whole life.
    virtual std::shared_ptr<pixel_buffer> pixels() const { return nullptr; }

    // what a terminal part sends, null for other components. the same stream
    // for the component's whole life. typed keys come as interact("key").
    virtual std::shared_ptr<byte_stream> terminal() const { return nullptr; }

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
