#pragma once

#include "../model/param.h"
#include "value.h"

#include <memory>
#include <span>
#include <string>

namespace logic::sim {

// behaviour of one instance of a primitive. ports are in the order of the
// primitive's interface ports: the simulator fills in the inputs, eval()
// writes the outputs.
class component {
public:
    virtual ~component() = default;

    // called whenever an input changed. must only depend on the inputs and
    // the state, outputs are compared afterwards to find changes.
    virtual void eval(std::span<value> ports) = 0;

    // one clock tick passed. returns true if the outputs may change, eval()
    // is called then.
    virtual bool tick() { return false; }

    // back to the state after creation
    virtual void reset() {}
};

class component_factory {
public:
    virtual ~component_factory() = default;

    // the parameters are resolved. null if the kind has no simulation model.
    virtual std::unique_ptr<component> create(const std::string& kind, const model::param_values& params) const = 0;
};

}
