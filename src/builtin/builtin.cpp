#include "builtin.h"

namespace logic::builtin {

const model::primitive_registry& registry()
{
    static const model::primitive_registry r = [] {
        model::primitive_registry r;
        add_gates(r);
        add_sources(r);
        return r;
    }();
    return r;
}

}
