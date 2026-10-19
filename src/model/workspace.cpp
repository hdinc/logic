#include "workspace.h"

#include <algorithm>
#include <cctype>
#include <random>

namespace logic::model {

library* workspace::find_library(const std::string& name)
{
    for (auto& l : libraries)
        if (l->name == name) return l.get();
    return nullptr;
}

library& workspace::add_library(const std::string& name)
{
    auto l = std::make_unique<library>();
    l->name = name;
    libraries.push_back(std::move(l));
    return *libraries.back();
}

cell* workspace::find_cell(id i)
{
    for (auto& l : libraries)
        if (auto c = l->find_cell(i)) return c;
    return nullptr;
}

const cell* workspace::find_cell(id i) const
{
    return const_cast<workspace*>(this)->find_cell(i);
}

library* workspace::library_of(id cell)
{
    for (auto& l : libraries)
        if (l->find_cell(cell)) return l.get();
    return nullptr;
}

cell& workspace::create_cell(library& lib, const std::string& name)
{
    auto c = std::make_unique<cell>();
    do {
        c->id = random_id();
    } while (find_cell(c->id));
    c->name = name;
    c->impl = schematic{};
    lib.cells.push_back(std::move(c));
    lib.dirty = true;
    return *lib.cells.back();
}

void workspace::delete_cell(id cell)
{
    for (auto& l : libraries) {
        auto n = std::erase_if(l->cells, [&](auto& c) { return c->id == cell; });
        if (n) l->dirty = true;
    }
}

std::vector<cell*> workspace::users_of(id cell)
{
    std::vector<model::cell*> r;
    for (auto& l : libraries) {
        for (auto& c : l->cells) {
            auto s = c->get_schematic();
            if (!s) continue;
            if (std::any_of(s->instances.begin(), s->instances.end(), [&](auto& i) { return i.cell == cell; }))
                r.push_back(c.get());
        }
    }
    return r;
}

bool workspace::would_create_cycle(id user, id used) const
{
    // cycle if `user` is reachable from `used` (or they are the same cell)
    std::vector<id> stack{used};
    std::vector<id> visited;

    while (!stack.empty()) {
        id c = stack.back();
        stack.pop_back();

        if (c == user) return true;
        if (std::find(visited.begin(), visited.end(), c) != visited.end()) continue;
        visited.push_back(c);

        auto cell = find_cell(c);
        if (!cell) continue;
        if (auto s = cell->get_schematic())
            for (auto& i : s->instances)
                stack.push_back(i.cell);
    }

    return false;
}

bool workspace::valid_cell_name(const library& lib, const std::string& name, std::string& error)
{
    if (name.empty()) {
        error = "name is empty";
        return false;
    }

    for (char ch : name) {
        if (!(std::isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == ' ' || ch == '.')) {
            error = "name can only contain letters, digits, space, '_', '-' and '.'";
            return false;
        }
    }

    if (name.front() == '.' || name.front() == ' ' || name.back() == ' ') {
        error = "name can't start with '.' or start/end with a space";
        return false;
    }

    for (auto& c : lib.cells) {
        if (c->name == name) {
            error = "a cell with this name already exists";
            return false;
        }
    }

    return true;
}

library& workspace::add_builtin_library(const primitive_registry& r, const std::string& name)
{
    primitives = &r;

    auto& lib = add_library(name);
    lib.read_only = true;
    for (auto& p : r.all()) {
        auto c = std::make_unique<cell>();
        c->id = builtin_cell_id(p->kind());
        c->name = p->name();
        c->impl = builtin_impl{p->kind()};
        auto iface = p->build(resolve_params(p->params(), {}));
        c->ports = std::move(iface.ports);
        c->symbol = std::move(iface.symbol);
        lib.cells.push_back(std::move(c));
    }
    return lib;
}

const primitive* workspace::primitive_of(const cell& c) const
{
    auto b = std::get_if<builtin_impl>(&c.impl);
    return b && primitives ? primitives->find(b->kind) : nullptr;
}

interface_view workspace::interface_of(const instance& i) const
{
    auto c = find_cell(i.cell);
    if (!c) return {};

    auto p = primitive_of(*c);
    static const std::vector<bit_link> no_links;
    static const std::vector<pull> no_pulls;
    if (!p) return {&c->ports, &c->symbol, &no_links, &no_pulls};

    auto values = resolve_params(p->params(), i.params);
    auto key = p->kind() + '\n' + params_key(values);
    auto it = interface_cache.find(key);
    if (it == interface_cache.end()) it = interface_cache.emplace(key, p->build(values)).first;
    return {&it->second.ports, &it->second.symbol, &it->second.links, &it->second.pulls};
}

std::vector<param_def> workspace::params_of(const instance& i) const
{
    auto c = find_cell(i.cell);
    auto p = c ? primitive_of(*c) : nullptr;
    return p ? p->params() : std::vector<param_def>{};
}

id workspace::random_id()
{
    static std::mt19937_64 rng{std::random_device{}()};
    id i;
    do {
        i = rng();
    } while (i == no_id);
    return i;
}

}
