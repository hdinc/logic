#include "workspace.h"
#include "expr.h"

#include <algorithm>
#include <cctype>
#include <functional>
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

const library* workspace::library_of(id cell) const
{
    return const_cast<workspace*>(this)->library_of(cell);
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

namespace {

// usable as a file name
bool valid_file_name(const std::string& name, std::string& error)
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

    return true;
}

}

bool workspace::valid_cell_name(const library& lib, const std::string& name, std::string& error)
{
    if (!valid_file_name(name, error)) return false;

    for (auto& c : lib.cells) {
        if (c->name == name) {
            error = "a cell with this name already exists";
            return false;
        }
    }

    return true;
}

bool workspace::valid_bundle_name(const library& lib, const std::string& name, std::string& error)
{
    if (!valid_file_name(name, error)) return false;

    for (auto& b : lib.bundles) {
        if (b->name == name) {
            error = "a bundle with this name already exists";
            return false;
        }
    }

    return true;
}

bundle_type* workspace::find_bundle(id i)
{
    for (auto& l : libraries)
        if (auto b = l->find_bundle(i)) return b;
    return nullptr;
}

const bundle_type* workspace::find_bundle(id i) const
{
    return const_cast<workspace*>(this)->find_bundle(i);
}

library* workspace::library_of_bundle(id bundle)
{
    for (auto& l : libraries)
        if (l->find_bundle(bundle)) return l.get();
    return nullptr;
}

const library* workspace::library_of_bundle(id bundle) const
{
    return const_cast<workspace*>(this)->library_of_bundle(bundle);
}

bundle_type& workspace::create_bundle(library& lib, const std::string& name)
{
    auto b = std::make_unique<bundle_type>();
    do {
        b->id = random_id();
    } while (find_bundle(b->id) || find_cell(b->id));
    b->name = name;
    lib.bundles.push_back(std::move(b));
    lib.dirty = true;
    return *lib.bundles.back();
}

void workspace::delete_bundle(id bundle)
{
    for (auto& l : libraries) {
        auto n = std::erase_if(l->bundles, [&](auto& b) { return b->id == bundle; });
        if (n) l->dirty = true;
    }
    bundles_changed();
}

std::vector<std::string> workspace::bundle_users(id bundle) const
{
    std::vector<std::string> r;
    auto ref = bundle_ref(bundle);
    for (auto& l : libraries) {
        for (auto& b : l->bundles)
            for (auto& f : b->fields)
                if (f.bundle == bundle) r.push_back("field " + f.name + " of bundle " + b->name);
        for (auto& c : l->cells) {
            for (auto& p : c->ports)
                if (p.bundle == bundle) r.push_back("port " + p.name + " of " + c->name);
            auto s = c->get_schematic();
            if (!s) continue;
            for (auto& i : s->instances) {
                for (auto& d : params_of(i)) {
                    auto v = i.params.find(d.name);
                    if (d.role != param_def::bundle || v == i.params.end() || v->second != param_value(ref)) continue;
                    r.push_back("instance " + i.name + " in " + c->name);
                    break;
                }
            }
        }
    }
    return r;
}

bool workspace::would_create_bundle_cycle(id outer, id inner) const
{
    // cycle if `outer` is reachable from `inner` (or they are the same)
    std::vector<id> stack{inner};
    std::vector<id> visited;

    while (!stack.empty()) {
        id b = stack.back();
        stack.pop_back();

        if (b == outer) return true;
        if (std::find(visited.begin(), visited.end(), b) != visited.end()) continue;
        visited.push_back(b);

        if (auto t = find_bundle(b))
            for (auto& f : t->fields)
                if (f.bundle != no_id) stack.push_back(f.bundle);
    }

    return false;
}

bundle_layout workspace::layout_of(id bundle, bool flipped, const param_values& args) const
{
    bundle_layout r;
    std::vector<id> open;

    // adds the fields of the type at bit offset, false on an error
    std::function<bool(id, bool, const param_values&, const std::string&, int, int&)> add = [&](id b, bool flip_view, const param_values& given,
                                                                                                    const std::string& prefix, int depth, int& offset) {
        auto t = find_bundle(b);
        if (!t) {
            r.error = "bundle type " + bundle_label(b) + " is missing";
            return false;
        }
        if (std::find(open.begin(), open.end(), b) != open.end()) {
            r.error = "bundle '" + t->name + "' contains itself";
            return false;
        }
        if (auto problem = check_bundle_args(b, given); !problem.empty()) {
            r.error = "bundle '" + t->name + "': " + problem;
            return false;
        }
        open.push_back(b);
        auto scope = bundle_values(b, given);

        for (auto& f : t->fields) {
            bundle_node n;
            n.name = f.name;
            n.path = prefix + f.name;
            n.depth = depth;
            n.offset = offset;
            if (f.bundle != no_id) {
                // the nested bundle's values from this one's
                param_values inner;
                for (auto& [name, text] : f.args) {
                    std::string error;
                    auto v = eval_expr(text, scope, error);
                    if (!v) {
                        r.error = "bundle '" + t->name + "', field " + f.name + ", " + name + ": " + error;
                        return false;
                    }
                    inner[name] = *v;
                }
                n.bundle = f.bundle;
                n.flipped = flip_view != f.flipped;
                n.args = bundle_values(f.bundle, inner);
                size_t at = r.nodes.size();
                r.nodes.push_back(n);
                if (!add(f.bundle, n.flipped, inner, n.path + ".", depth + 1, offset)) return false;
                r.nodes[at].width = offset - n.offset;
            } else {
                std::string error;
                auto w = eval_expr(f.width, scope, error);
                if (!w) {
                    r.error = "bundle '" + t->name + "', width of " + f.name + ": " + error;
                    return false;
                }
                if (*w < 0 || *w > max_width) {
                    r.error = "bundle '" + t->name + "': field " + f.name + " has a width of " + std::to_string(*w);
                    return false;
                }
                // an optional field that is off
                if (*w == 0) continue;
                n.width = (int)*w;
                n.dir = flip_view ? flip(f.dir) : f.dir;
                r.nodes.push_back(n);
                r.leaves.push_back({n.path, n.dir, offset, n.width});
                offset += n.width;
            }
            if (offset > max_width) {
                r.error = "bundle '" + t->name + "' has more than " + std::to_string(max_width) + " bits";
                return false;
            }
        }

        open.pop_back();
        return true;
    };

    int width = 0;
    if (!add(bundle, flipped, args, "", 0, width)) {
        auto error = std::move(r.error);
        r = {};
        r.error = std::move(error);
        return r;
    }
    r.width = width;
    return r;
}

std::string workspace::bundle_label(id bundle, bool flipped, const param_values& args) const
{
    auto t = find_bundle(bundle);
    auto name = view_name(t ? t->name : "#" + std::to_string(bundle), flipped);
    if (!t) return name;
    auto values = bundle_args(bundle, args);
    if (values.empty()) return name;
    if (t->params.size() == 1) return name + "<" + std::to_string(std::get<int64_t>(values.begin()->second)) + ">";
    // in the order of the parameters
    std::string list;
    for (auto& d : t->params)
        if (auto it = values.find(d.name); it != values.end())
            list += (list.empty() ? "" : ",") + d.name + "=" + std::to_string(std::get<int64_t>(it->second));
    return name + "<" + list + ">";
}

param_values workspace::bundle_values(id bundle, const param_values& args) const
{
    param_values r;
    auto t = find_bundle(bundle);
    if (!t) return r;
    for (auto& d : t->params) {
        auto it = args.find(d.name);
        r[d.name] = it != args.end() && std::holds_alternative<int64_t>(it->second) ? it->second : d.def;
    }
    return r;
}

param_values workspace::bundle_args(id bundle, const param_values& args) const
{
    param_values r;
    auto t = find_bundle(bundle);
    if (!t) return r;
    for (auto& d : t->params) {
        auto it = args.find(d.name);
        if (it != args.end() && std::holds_alternative<int64_t>(it->second) && it->second != d.def) r[d.name] = it->second;
    }
    return r;
}

std::string workspace::check_bundle_args(id bundle, const param_values& args) const
{
    auto t = find_bundle(bundle);
    if (!t) return {};
    for (auto& [name, v] : args) {
        auto d = std::find_if(t->params.begin(), t->params.end(), [&](const param_def& d) { return d.name == name; });
        if (d == t->params.end()) return "it has no parameter " + name;
        auto i = std::get_if<int64_t>(&v);
        if (!i) return name + " is not an integer";
        if (*i < d->min || *i > d->max)
            return name + " = " + std::to_string(*i) + " is out of range " + std::to_string(d->min) + ".." + std::to_string(d->max);
    }
    return {};
}

bool workspace::same_bundle(id a, const param_values& a_args, id b, const param_values& b_args) const
{
    return a == b && (a == no_id || bundle_values(a, a_args) == bundle_values(b, b_args));
}

std::optional<param_values> workspace::group_args(const port_group& g, const param_values& model_values, std::string& error) const
{
    param_values r;
    for (auto& [name, text] : g.args) {
        std::string e;
        auto v = eval_expr(text, model_values, e);
        if (!v) {
            error = name + ": " + e;
            return std::nullopt;
        }
        r[name] = *v;
    }
    return bundle_args(g.bundle, r);
}

workspace::grouping workspace::group_ports(const std::vector<port>& leaves, const std::vector<port_group>& groups, const param_values& model_values) const
{
    grouping r;
    // per leaf the group it's in and its offset there, -1 for none
    std::vector<int> group_of(leaves.size(), -1);
    std::vector<int> bit_of(leaves.size(), 0);
    std::vector<int> width_of(groups.size(), 0);
    std::vector<param_values> args_of(groups.size());

    for (int g = 0; g < (int)groups.size(); g++) {
        auto& grp = groups[g];
        auto what = "bundle port " + grp.name + ": ";
        std::string error;
        auto args = group_args(grp, model_values, error);
        if (!args) {
            r.problems.push_back(what + error);
            continue;
        }
        args_of[g] = *args;
        auto layout = layout_of(grp.bundle, grp.flipped, *args);
        if (!layout) {
            r.problems.push_back(what + layout.error);
            continue;
        }
        if (layout.leaves.empty()) {
            r.problems.push_back(what + "the bundle has no fields");
            continue;
        }
        std::vector<std::pair<int, int>> found;
        std::string problem;
        for (auto& f : layout.leaves) {
            auto name = field_port_name(grp, f.path);
            auto it = std::find_if(leaves.begin(), leaves.end(), [&](const port& p) { return p.name == name; });
            int k = (int)(it - leaves.begin());
            if (it == leaves.end())
                problem = "the model has no port " + name;
            else if (group_of[k] >= 0)
                problem = name + " is already in bundle port " + groups[group_of[k]].name;
            else if (it->width != f.width)
                problem = name + " has " + std::to_string(it->width) + " bits, the field " + std::to_string(f.width);
            else if (it->dir != f.dir)
                problem = name + " is an " + to_string(it->dir) + ", the field an " + to_string(f.dir);
            if (!problem.empty()) break;
            found.push_back({k, f.offset});
        }
        if (!problem.empty()) {
            r.problems.push_back(what + problem);
            continue;
        }
        for (auto [k, bit] : found) {
            group_of[k] = g;
            bit_of[k] = bit;
        }
        width_of[g] = layout.width;
    }

    // the bundle port where its first field was
    std::vector<int> port_of_group(groups.size(), -1);
    r.model_ports.resize(leaves.size());
    for (int k = 0; k < (int)leaves.size(); k++) {
        int g = group_of[k];
        if (g < 0) {
            r.model_ports[k] = {leaves[k], (int)r.ports.size(), 0};
            r.ports.push_back(leaves[k]);
            continue;
        }
        if (port_of_group[g] < 0) {
            auto& grp = groups[g];
            port p;
            p.name = grp.name;
            p.dir = grp.side;
            p.width = width_of[g];
            p.description = grp.description;
            p.bundle = grp.bundle;
            p.flipped = grp.flipped;
            p.bundle_args = args_of[g];
            port_of_group[g] = (int)r.ports.size();
            r.ports.push_back(p);
        }
        r.model_ports[k] = {leaves[k], port_of_group[g], bit_of[k]};
    }
    return r;
}

port_group workspace::bundle_from_ports(library& lib, const std::vector<port>& ports, const port_prefix& p)
{
    // io_jtag_ -> io_jtag -> jtag
    auto base = p.prefix.substr(0, p.prefix.size() - 1);
    auto last = base.substr(base.rfind('_') == std::string::npos ? 0 : base.rfind('_') + 1);
    std::string error;
    auto name = last;
    for (int n = 2; !valid_bundle_name(lib, name, error); n++)
        name = (last.empty() ? "bundle" : last) + std::to_string(n);

    auto& b = create_bundle(lib, name);
    int inputs = 0;
    for (int k : p.ports) {
        auto& port = ports[k];
        bundle_field f;
        f.name = port.name.substr(p.prefix.size());
        f.width = std::to_string(port.width);
        f.dir = port.dir;
        f.description = port.description;
        b.fields.push_back(f);
        if (port.dir == direction::input) inputs++;
    }

    // the model's view is the normal one
    port_group g;
    g.name = base;
    g.bundle = b.id;
    g.side = 2 * inputs > (int)p.ports.size() ? direction::input : direction::output;
    return g;
}

std::vector<std::string> workspace::apply_groups(cell& c)
{
    auto groups = c.port_groups();
    auto leaves = c.leaf_ports();
    if (!groups || !leaves) return {};
    auto before = c.ports;

    if (groups->empty()) {
        // back to the model's ports
        if (!leaves->empty()) c.set_ports(*leaves);
        leaves->clear();
    } else {
        if (leaves->empty()) *leaves = c.ports;
        auto g = group_ports(*leaves, *groups, resolve_params(*c.external_params(), {}));
        c.set_ports(g.ports);
        if (c.ports != before)
            if (auto l = library_of(c.id)) l->dirty = true;
        return g.problems;
    }
    if (c.ports != before)
        if (auto l = library_of(c.id)) l->dirty = true;
    return {};
}

std::vector<model_port> workspace::map_model_ports(const std::vector<port>& ports, const std::vector<port>& leaves, const std::vector<port_group>& groups,
    const param_values& model_values) const
{
    auto g = group_ports(leaves, groups, model_values);
    std::vector<model_port> r;
    for (auto& m : g.model_ports) {
        auto& name = g.ports[m.of].name;
        auto it = std::find_if(ports.begin(), ports.end(), [&](const port& p) { return p.name == name; });
        // the ports are from before a change of the groups or bundles
        if (it == ports.end() || it->width < m.bit + m.port.width) return {};
        r.push_back({m.port, (int)(it - ports.begin()), m.bit});
    }
    return r;
}

void workspace::bundles_changed()
{
    m_bundle_revision++;

    for (auto& l : libraries) {
        for (auto& c : l->cells) {
            if (auto g = c->port_groups(); g && !g->empty()) apply_groups(*c);
            for (auto& p : c->ports) {
                if (p.bundle == no_id) continue;
                auto layout = layout_of(p.bundle, false, p.bundle_args);
                // an empty bundle still needs a bit for the net
                int w = layout ? std::max(layout.width, 1) : p.width;
                if (p.width == w) continue;
                p.width = w;
                l->dirty = true;
            }
        }
    }
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
        c->category = p->category();
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
    static const std::vector<model_port> no_model_ports;
    if (!p) {
        if (auto e = external_interface(*c, i)) return {&e->iface.ports, &e->iface.symbol, &no_links, &no_pulls, &e->iface.model_ports};
        if (c->auto_symbol()) return {&c->ports, &auto_symbol_of(*c), &no_links, &no_pulls, &no_model_ports};
        auto groups = c->port_groups();
        if (!groups || groups->empty()) return {&c->ports, &c->symbol, &no_links, &no_pulls, &no_model_ports};

        // the model's ports of the grouped ones
        auto& e = group_cache[c->id];
        if (e.ports != c->ports || e.leaves != c->model_ports() || e.revision != m_bundle_revision) {
            e.ports = c->ports;
            e.leaves = c->model_ports();
            e.revision = m_bundle_revision;
            e.model_ports = map_model_ports(c->ports, e.leaves, *groups, resolve_params(*c->external_params(), {}));
        }
        return {&c->ports, &c->symbol, &no_links, &no_pulls, &e.model_ports};
    }

    auto values = resolve_params(p->params(), i.params);
    auto key = p->kind() + '\n' + params_key(values);
    if (p->uses_bundles()) key += '\n' + std::to_string(m_bundle_revision);
    auto it = interface_cache.find(key);
    if (it == interface_cache.end()) it = interface_cache.emplace(key, p->build(values, build_context{this})).first;
    return {&it->second.ports, &it->second.symbol, &it->second.links, &it->second.pulls, &it->second.model_ports, &it->second.tunnel};
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
    if (auto p = primitive_of(*c)) return p->check(resolve_params(p->params(), i.params), build_context{this});
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

const model::symbol& workspace::auto_symbol_of(const cell& c) const
{
    auto& e = auto_symbols[c.id];
    if (e.symbol.shapes.empty() || e.ports != c.ports || e.name != c.name) {
        e.ports = c.ports;
        e.name = c.name;
        id next = 1;
        e.symbol = generated_symbol(c.ports, c.name, pin_order::position, [&] { return next++; });
    }
    return e.symbol;
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

    if (auto groups = c.port_groups(); groups && !groups->empty()) {
        *c.leaf_ports() = std::move(ports);
        apply_groups(c);
    } else {
        c.set_ports(ports);
    }
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
    if (it != external_cache.end() && it->second.cell_ports == c.ports && it->second.cell_symbol == c.symbol && it->second.revision == m_bundle_revision)
        return &it->second;

    external_entry e;
    e.cell_ports = c.ports;
    e.cell_symbol = c.symbol;
    e.revision = m_bundle_revision;
    e.iface.ports = c.ports;
    e.iface.symbol = c.symbol;

    std::vector<port> ports;
    auto p = provider_of(c);
    auto lib = library_of(c.id);
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
        auto groups = c.port_groups();
        if (groups && !groups->empty()) {
            // the model's ports for these parameters, grouped
            auto g = group_ports(ports, *groups, values);
            tmp.set_ports(g.ports);
            e.iface.model_ports = map_model_ports(tmp.ports, ports, *groups, values);
        } else {
            tmp.set_ports(ports);
        }
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
