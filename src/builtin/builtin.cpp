#include "builtin.h"

namespace logic::builtin {

namespace {

class builtin_factory : public sim::component_factory {
public:
    std::unique_ptr<sim::component> create(const std::string& kind, const model::param_values& params) const override
    {
        // the registry only holds builtin::primitive
        auto p = static_cast<const primitive*>(registry().find(kind));
        return p ? p->create(params) : nullptr;
    }
};

}

const model::primitive_registry& registry()
{
    static const model::primitive_registry r = [] {
        model::primitive_registry r;
        add_sources(r);
        add_gates(r);
        add_memory(r);
        add_wiring(r);
        return r;
    }();
    return r;
}

const sim::component_factory& factory()
{
    static const builtin_factory f;
    return f;
}

}
