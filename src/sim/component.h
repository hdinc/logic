#pragma once

#include "../model/param.h"
#include "value.h"

#include <cstdint>
#include <limits>
#include <memory>
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
    explicit context(time now, bool woken, time wake)
        : m_now(now)
        , m_woken(woken)
        , m_wake(wake)
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

private:
    time m_now;
    bool m_woken;
    time m_wake;
};

// a named part of a component's state, shown by debugging views
struct inspect_item {
    std::string name;
    value v;
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

    // copies the internal state into bytes and back, for snapshots of the
    // whole simulation. false if the component doesn't support it.
    virtual bool save_state(std::vector<uint8_t>&) const { return false; }
    virtual bool load_state(std::span<const uint8_t>) { return false; }
};

// creates the simulation models of cells that aren't schematics
class component_factory {
public:
    virtual ~component_factory() = default;

    // the parameters are resolved. null if the factory doesn't handle the
    // cell, or with an error if it does but can't create the model.
    virtual std::unique_ptr<component> create(const model::cell& c, const model::param_values& params, std::string& error) const = 0;
};

// asks factories in order until one creates the model or reports an error
class factory_chain : public component_factory {
public:
    explicit factory_chain(std::vector<const component_factory*> list)
        : list(std::move(list))
    {
    }

    std::unique_ptr<component> create(const model::cell& c, const model::param_values& params, std::string& error) const override
    {
        for (auto f : list) {
            auto r = f->create(c, params, error);
            if (r || !error.empty()) return r;
        }
        return nullptr;
    }

private:
    std::vector<const component_factory*> list;
};

}
