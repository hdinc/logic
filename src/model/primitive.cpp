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

void primitive_registry::add_bundle(bundle_type b)
{
    b.id = builtin_bundle_id(b.name);
    for (auto& o : bundle_list)
        if (o.id == b.id) throw std::logic_error("bundle '" + b.name + "' registered twice");
    bundle_list.push_back(std::move(b));
}

namespace {

// 64 bit fnv-1a
id fnv(const std::string& s)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (char ch : s) {
        h ^= (unsigned char)ch;
        h *= 0x100000001b3ull;
    }
    return h == no_id ? 1 : h;
}

}

id builtin_cell_id(const std::string& kind)
{
    return fnv("builtin/" + kind);
}

id builtin_bundle_id(const std::string& name)
{
    return fnv("builtin/bundle/" + name);
}

}
