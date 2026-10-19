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

cell& workspace::create_cell(library& lib, const std::string& name, implementation impl)
{
    auto c = std::make_unique<cell>();
    do {
        c->id = random_id();
    } while (find_cell(c->id));
    c->name = name;
    c->impl = std::move(impl);
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
    if (!p) {
        if (auto e = external_interface(*c, i)) return {&e->iface.ports, &e->iface.symbol, &no_links, &no_pulls};
        return {&c->ports, &c->symbol, &no_links, &no_pulls};
    }

    auto values = resolve_params(p->params(), i.params);
    auto key = p->kind() + '\n' + params_key(values);
    auto it = interface_cache.find(key);
    if (it == interface_cache.end()) it = interface_cache.emplace(key, p->build(values)).first;
    return {&it->second.ports, &it->second.symbol, &it->second.links, &it->second.pulls};
}

std::vector<param_def> workspace::params_of(const instance& i) const
{
    auto c = find_cell(i.cell);
    if (!c) return {};
    if (auto p = primitive_of(*c)) return p->params();
    if (auto defs = c->external_params()) return *defs;
    return {};
}

std::string workspace::check_instance(const instance& i) const
{
    auto c = find_cell(i.cell);
    if (!c) return {};
    if (auto p = primitive_of(*c)) return p->check(resolve_params(p->params(), i.params));
    if (auto e = external_interface(*c, i)) return e->error;
    return {};
}

void workspace::add_provider(const external_provider& p)
{
    providers.push_back(&p);
}

const external_provider* workspace::provider_of(const cell& c) const
{
    for (auto p : providers)
        if (p->handles(c)) return p;
    return nullptr;
}

bool workspace::refresh_interface(cell& c, std::string& error)
{
    auto params = c.external_params();
    if (!params) {
        error = "'" + c.name + "' is not a c model or hdl cell";
        return false;
    }
    auto p = provider_of(c);
    if (!p) {
        error = "nothing can read this kind of model yet";
        return false;
    }

    auto lib = library_of(c.id);
    std::vector<port> ports;
    std::vector<param_def> defs;
    if (!p->describe(c, lib ? lib->dir : std::filesystem::path{}, ports, defs, error)) return false;

    c.set_ports(ports);
    *params = std::move(defs);
    if (lib) lib->dirty = true;
    return true;
}

const workspace::external_entry* workspace::external_interface(const cell& c, const instance& i) const
{
    auto defs = c.external_params();
    if (!defs) return nullptr;

    // the cell's own interface is the one for the defaults
    auto values = resolve_params(*defs, i.params);
    if (values == resolve_params(*defs, {})) return nullptr;

    auto key = std::to_string(c.id) + '\n' + params_key(values);
    auto it = external_cache.find(key);
    if (it != external_cache.end() && it->second.cell_ports == c.ports && it->second.cell_symbol == c.symbol) return &it->second;

    external_entry e;
    e.cell_ports = c.ports;
    e.cell_symbol = c.symbol;
    e.iface.ports = c.ports;
    e.iface.symbol = c.symbol;

    std::vector<port> ports;
    auto p = provider_of(c);
    auto lib = const_cast<workspace*>(this)->library_of(c.id);
    if (!p) {
        e.error = "the model can't be read, the parameters don't change its ports";
    } else if (!p->ports_for(c, lib ? lib->dir : std::filesystem::path{}, values, ports, e.error)) {
        e.error = "the ports for these parameters are unknown: " + e.error;
    } else {
        // the same ports with other widths keep the symbol
        cell tmp;
        tmp.ports = c.ports;
        tmp.symbol = c.symbol;
        tmp.next_id = c.next_id;
        tmp.name = c.name;
        tmp.set_ports(ports);
        e.iface.ports = std::move(tmp.ports);
        e.iface.symbol = std::move(tmp.symbol);
    }

    return &(external_cache[key] = std::move(e));
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
