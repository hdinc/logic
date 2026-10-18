#include "primitive.h"

#include <stdexcept>

namespace logic::model {

void primitive_registry::add(std::unique_ptr<primitive> p)
{
    if (find(p->kind())) throw std::logic_error("primitive '" + p->kind() + "' registered twice");
    list.push_back(std::move(p));
}

const primitive* primitive_registry::find(const std::string& kind) const
{
    for (auto& p : list)
        if (p->kind() == kind) return p.get();
    return nullptr;
}

id builtin_cell_id(const std::string& kind)
{
    // 64 bit fnv-1a
    uint64_t h = 0xcbf29ce484222325ull;
    for (char ch : "builtin/" + kind) {
        h ^= (unsigned char)ch;
        h *= 0x100000001b3ull;
    }
    return h == no_id ? 1 : h;
}

}
