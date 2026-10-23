#include "library.h"

#include <algorithm>
#include <set>

namespace logic::model {

cell* library::find_cell(id i)
{
    for (auto& c : cells)
        if (c->id == i) return c.get();
    return nullptr;
}

cell* library::find_cell(const std::string& name)
{
    for (auto& c : cells)
        if (c->name == name) return c.get();
    return nullptr;
}

bundle_type* library::find_bundle(id i)
{
    for (auto& b : bundles)
        if (b->id == i) return b.get();
    return nullptr;
}

bundle_type* library::find_bundle(const std::string& name)
{
    for (auto& b : bundles)
        if (b->name == name) return b.get();
    return nullptr;
}

std::vector<std::string> library::categories() const
{
    std::set<std::string> r;
    for (auto& c : cells)
        if (!c->category.empty()) r.insert(c->category);
    return {r.begin(), r.end()};
}

std::string library::unique_cell_name(const std::string& prefix) const
{
    for (int n = 1;; n++) {
        auto name = prefix + std::to_string(n);
        if (std::none_of(cells.begin(), cells.end(), [&](auto& c) { return c->name == name; }))
            return name;
    }
}

std::string library::unique_bundle_name(const std::string& prefix) const
{
    for (int n = 1;; n++) {
        auto name = prefix + std::to_string(n);
        if (std::none_of(bundles.begin(), bundles.end(), [&](auto& b) { return b->name == name; }))
            return name;
    }
}

}
