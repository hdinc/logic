#include "library.h"

#include <algorithm>

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

std::string library::unique_cell_name(const std::string& prefix) const
{
    for (int n = 1;; n++) {
        auto name = prefix + std::to_string(n);
        if (std::none_of(cells.begin(), cells.end(), [&](auto& c) { return c->name == name; }))
            return name;
    }
}

}
