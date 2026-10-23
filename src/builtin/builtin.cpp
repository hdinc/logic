#include "builtin.h"

namespace logic::builtin {

namespace {

class builtin_factory : public sim::component_factory {
public:
    std::unique_ptr<sim::component> create(const sim::create_info& info, std::string& error) const override
    {
        auto b = std::get_if<model::builtin_impl>(&info.cell.impl);
        // the registry only holds builtin::primitive
        auto p = b ? static_cast<const primitive*>(registry().find(b->kind)) : nullptr;
        return p ? p->create(info, error) : nullptr;
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
        add_arith(r);
        add_plexers(r);
        add_wiring(r);
        add_io(r);
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
