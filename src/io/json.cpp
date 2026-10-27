#include "json.h"

#include <algorithm>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace logic::io {

using nlohmann::json;
using namespace model;

namespace {

json vec(glm::ivec2 v) { return json::array({v.x, v.y}); }
glm::ivec2 vec(const json& j) { return {j.at(0).get<int>(), j.at(1).get<int>()}; }

direction direction_from_string(const std::string& s)
{
    if (s == "input") return direction::input;
    if (s == "output") return direction::output;
    if (s == "inout") return direction::inout;
    throw std::runtime_error("invalid port direction '" + s + "'");
}

json shape_to_json(const shape& s)
{
    json j;
    j["id"] = s.id;
    if (auto l = std::get_if<line>(&s.geom)) {
        j["type"] = "line";
        j["a"] = vec(l->a);
        j["b"] = vec(l->b);
    } else if (auto b = std::get_if<bezier>(&s.geom)) {
        j["type"] = "bezier";
        j["a"] = vec(b->a);
        j["b"] = vec(b->b);
        j["c"] = vec(b->c);
    } else if (auto a = std::get_if<arc>(&s.geom)) {
        j["type"] = "arc";
        j["a"] = vec(a->a);
        j["b"] = vec(a->b);
        j["c"] = vec(a->c);
    } else if (auto c = std::get_if<circle>(&s.geom)) {
        j["type"] = "circle";
        j["center"] = vec(c->center);
        j["radius"] = c->radius;
    } else if (auto t = std::get_if<text>(&s.geom)) {
        j["type"] = "text";
        j["pos"] = vec(t->pos);
        j["text"] = t->str;
        j["size"] = t->size;
        if (t->align == text_align::right) j["align"] = "right";
        if (t->align == text_align::center) j["align"] = "center";
    }
    if (s.width != 0.0f) j["width"] = s.width;
    return j;
}

const char* param_type_names[] = {"integer", "boolean", "choice", "text"};

json param_value_json(const param_value& v)
{
    json j;
    std::visit([&](auto& x) { j = x; }, v);
    return j;
}

// null if the json type isn't one of the parameter value types
std::optional<param_value> param_value_from_json(const json& v)
{
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number_integer()) return v.get<int64_t>();
    if (v.is_string()) return v.get<std::string>();
    return std::nullopt;
}

json params_to_json(const std::vector<param_def>& defs)
{
    json a = json::array();
    for (auto& d : defs) {
        json j{{"name", d.name}, {"type", param_type_names[d.type]}, {"default", param_value_json(d.def)}};
        if (!d.label.empty() && d.label != d.name) j["label"] = d.label;
        if (d.type == param_def::integer) {
            j["min"] = d.min;
            j["max"] = d.max;
        }
        if (d.type == param_def::choice) j["choices"] = d.choices;
        if (d.file) j["file"] = true;
        if (!d.description.empty()) j["description"] = d.description;
        a.push_back(std::move(j));
    }
    return a;
}

std::vector<param_def> params_from_json(const json& a)
{
    std::vector<param_def> r;
    for (auto& j : a) {
        param_def d;
        d.name = j.at("name");
        d.label = j.value("label", d.name);
        auto type = j.at("type").get<std::string>();
        auto t = std::find(std::begin(param_type_names), std::end(param_type_names), type);
        if (t == std::end(param_type_names)) throw std::runtime_error("invalid parameter type '" + type + "'");
        d.type = (param_def::type_t)(t - std::begin(param_type_names));
        auto def = param_value_from_json(j.at("default"));
        if (!def) throw std::runtime_error("invalid default of parameter '" + d.name + "'");
        d.def = *def;
        d.min = j.value("min", int64_t(0));
        d.max = j.value("max", int64_t(0));
        if (j.contains("choices")) d.choices = j.at("choices").get<std::vector<std::string>>();
        d.file = j.value("file", false);
        d.description = j.value("description", "");
        r.push_back(std::move(d));
    }
    return r;
}

template <class E, size_t N>
E enum_from_string(const std::string& s, const char* const (&names)[N], const char* what)
{
    for (size_t k = 0; k < N; k++)
        if (s == names[k]) return (E)k;
    throw std::runtime_error(std::string("invalid ") + what + " '" + s + "'");
}

const char* const language_names[] = {"verilog", "systemverilog", "vhdl"};
const char* const backend_names[] = {"verilator", "icarus", "ghdl"};

shape shape_from_json(const json& j)
{
    shape s;
    s.id = j.value("id", model::id{0});
    auto type = j.at("type").get<std::string>();
    if (type == "line") {
        s.geom = line{vec(j.at("a")), vec(j.at("b"))};
    } else if (type == "bezier") {
        s.geom = bezier{vec(j.at("a")), vec(j.at("b")), vec(j.at("c"))};
    } else if (type == "arc") {
        s.geom = arc{vec(j.at("a")), vec(j.at("b")), vec(j.at("c"))};
    } else if (type == "circle") {
        s.geom = circle{vec(j.at("center")), j.at("radius").get<float>()};
    } else if (type == "text") {
        text t{vec(j.at("pos")), j.at("text"), j.at("size").get<float>()};
        auto align = j.value("align", "left");
        if (align == "right") t.align = text_align::right;
        if (align == "center") t.align = text_align::center;
        s.geom = t;
    } else {
        throw std::runtime_error("invalid shape type '" + type + "'");
    }
    if (j.contains("width")) s.width = std::max(j.at("width").get<float>(), 0.0f);
    return s;
}

}

std::string to_clipboard(const cell& part, const std::string& what)
{
    // the id only has to be valid
    cell c = part;
    c.id = 1;
    return json{{"logic_clipboard", what}, {"cell", to_json(c)}}.dump();
}

std::optional<cell> from_clipboard(const std::string& text, const std::string& what)
{
    try {
        auto j = json::parse(text);
        if (!j.is_object() || j.value("logic_clipboard", "") != what) return std::nullopt;
        return cell_from_json(j.at("cell"));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string id_to_string(model::id i)
{
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)i);
    return buf;
}

model::id id_from_string(const std::string& s)
{
    size_t n = 0;
    auto i = std::stoull(s, &n, 16);
    if (n != s.size() || i == no_id) throw std::runtime_error("invalid id '" + s + "'");
    return i;
}

namespace {

json port_to_json(const port& p)
{
    json j{
        {"id", p.id},
        {"name", p.name},
        {"dir", to_string(p.dir)},
        {"width", p.width},
        {"pos", vec(p.pos)},
    };
    if (p.xf.rotation) j["rotation"] = p.xf.rotation;
    if (p.xf.mirror) j["mirror"] = true;
    if (!p.description.empty()) j["description"] = p.description;
    if (p.bundle != no_id) {
        j["bundle"] = id_to_string(p.bundle);
        if (p.flipped) j["flipped"] = true;
    }
    if (p.format != number_format{}) {
        j["format"] = {{"kind", kind_id(p.format.kind)}};
        if (p.format.frac) j["format"]["frac"] = p.format.frac;
    }
    return j;
}

port port_from_json(const json& j)
{
    port r;
    r.id = j.at("id");
    r.name = j.at("name");
    r.dir = direction_from_string(j.at("dir"));
    r.width = j.value("width", 1);
    r.pos = vec(j.at("pos"));
    r.xf.rotation = ((j.value("rotation", 0) % 4) + 4) % 4;
    r.xf.mirror = j.value("mirror", false);
    r.description = j.value("description", "");
    if (j.contains("bundle")) {
        r.bundle = id_from_string(j.at("bundle"));
        r.flipped = j.value("flipped", false);
    }
    // an unknown kind (from a newer version) shows as hex
    if (auto it = j.find("format"); it != j.end() && it->is_object()) {
        r.format.kind = parse_number_kind(it->value("kind", "hex")).value_or(number_format::hex);
        r.format.frac = std::clamp(it->value("frac", 0), 0, 64);
    }
    return r;
}

// the port groups and the model's ports of an external cell, only if there
// are groups
void groups_to_json(json& impl, const std::vector<port_group>& groups, const std::vector<port>& leaves)
{
    if (groups.empty()) return;
    auto& a = impl["groups"] = json::array();
    for (auto& g : groups) {
        json jg{{"name", g.name}, {"bundle", id_to_string(g.bundle)}, {"side", to_string(g.side)}};
        if (g.flipped) jg["flipped"] = true;
        if (!g.prefix.empty()) jg["prefix"] = g.prefix;
        if (!g.description.empty()) jg["description"] = g.description;
        a.push_back(std::move(jg));
    }
    auto& l = impl["leaf_ports"] = json::array();
    for (auto& p : leaves)
        l.push_back(port_to_json(p));
}

void groups_from_json(const json& impl, std::vector<port_group>& groups, std::vector<port>& leaves)
{
    for (auto& jg : impl.value("groups", json::array())) {
        port_group g;
        g.name = jg.at("name");
        g.bundle = id_from_string(jg.at("bundle"));
        g.flipped = jg.value("flipped", false);
        g.prefix = jg.value("prefix", "");
        g.side = direction_from_string(jg.value("side", "output"));
        g.description = jg.value("description", "");
        groups.push_back(std::move(g));
    }
    for (auto& p : impl.value("leaf_ports", json::array()))
        leaves.push_back(port_from_json(p));
}

}

json to_json(const cell& c)
{
    json j;
    j["format"] = format_version;
    j["id"] = id_to_string(c.id);
    j["name"] = c.name;
    if (!c.category.empty()) j["category"] = c.category;
    if (c.fill) {
        char buf[16];
        auto byte = [](float f) { return (int)std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f); };
        std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", byte(c.fill->r), byte(c.fill->g), byte(c.fill->b), byte(c.fill->a));
        j["fill"] = buf;
    }
    j["next_id"] = c.next_id;

    j["ports"] = json::array();
    for (auto& p : c.ports)
        j["ports"].push_back(port_to_json(p));

    auto& sym = j["symbol"];
    sym["shapes"] = json::array();
    for (auto& s : c.symbol.shapes)
        sym["shapes"].push_back(shape_to_json(s));
    sym["pins"] = json::array();
    for (auto& p : c.symbol.pins)
        sym["pins"].push_back({{"id", p.id}, {"port", p.port}, {"pos", vec(p.pos)}});

    auto& impl = j["impl"];
    if (auto s = c.get_schematic()) {
        impl["type"] = "schematic";
        impl["wires"] = json::array();
        for (auto& w : s->wires)
            impl["wires"].push_back({{"id", w.id}, {"a", vec(w.a)}, {"b", vec(w.b)}});
        impl["instances"] = json::array();
        for (auto& i : s->instances) {
            impl["instances"].push_back({
                {"id", i.id},
                {"cell", id_to_string(i.cell)},
                {"name", i.name},
                {"pos", vec(i.pos)},
                {"rotation", i.xf.rotation},
                {"mirror", i.xf.mirror},
            });
            if (!i.params.empty()) {
                auto& params = impl["instances"].back()["params"];
                for (auto& [name, v] : i.params)
                    params[name] = param_value_json(v);
            }
        }
    } else if (auto b = std::get_if<builtin_impl>(&c.impl)) {
        impl["type"] = "builtin";
        impl["kind"] = b->kind;
    } else if (auto m = std::get_if<c_model_impl>(&c.impl)) {
        impl["type"] = "c_model";
        impl["plugin"] = m->plugin;
        impl["model"] = m->model;
        impl["params"] = params_to_json(m->params);
        groups_to_json(impl, m->groups, m->leaf_ports);
    } else if (auto h = std::get_if<hdl_impl>(&c.impl)) {
        impl["type"] = "hdl";
        impl["language"] = to_string(h->language);
        impl["backend"] = to_string(h->backend);
        impl["sources"] = h->sources;
        impl["top"] = h->top;
        if (!h->include_dirs.empty()) impl["include_dirs"] = h->include_dirs;
        if (!h->defines.empty()) impl["defines"] = h->defines;
        if (!h->args.empty()) impl["args"] = h->args;
        impl["params"] = params_to_json(h->params);
        groups_to_json(impl, h->groups, h->leaf_ports);
    } else if (auto u = std::get_if<unknown_impl>(&c.impl)) {
        impl = json::parse(u->json);
    }

    return j;
}

symbol symbol_for(cell& c, const json& j)
{
    const json* sym = &j;
    // a cell file's pins name its ports by id
    std::map<id, std::string> file_ports;
    if (j.contains("symbol")) {
        sym = &j.at("symbol");
        for (auto& p : j.at("ports"))
            file_ports[p.at("id").get<id>()] = p.at("name").get<std::string>();
    }

    symbol s;
    for (auto& js : sym->at("shapes")) {
        auto sh = shape_from_json(js);
        sh.id = c.new_id();
        s.shapes.push_back(std::move(sh));
    }
    for (auto& jp : sym->at("pins")) {
        auto& pj = jp.at("port");
        std::string name;
        if (pj.is_string()) {
            name = pj.get<std::string>();
        } else {
            auto it = file_ports.find(pj.get<id>());
            if (it == file_ports.end()) throw std::runtime_error("a pin of an unknown port");
            name = it->second;
        }
        auto port = std::find_if(c.ports.begin(), c.ports.end(), [&](auto& p) { return p.name == name; });
        if (port == c.ports.end()) throw std::runtime_error("no port '" + name + "'");
        s.pins.push_back({c.new_id(), port->id, vec(jp.at("pos"))});
    }
    for (auto& p : c.ports)
        if (!s.find_pin_of_port(p.id)) throw std::runtime_error("no pin for port '" + p.name + "'");
    return s;
}

cell cell_from_json(const json& j)
{
    int format = j.at("format");
    if (format > format_version)
        throw std::runtime_error("file format " + std::to_string(format) + " is newer than supported (" + std::to_string(format_version) + ")");

    cell c;
    c.id = id_from_string(j.at("id"));
    c.name = j.at("name");
    c.category = j.value("category", "");
    // #RRGGBBAA, anything else is no color of its own
    if (auto f = j.value("fill", std::string()); f.size() == 9 && f[0] == '#') {
        auto v = std::strtoul(f.c_str() + 1, nullptr, 16);
        c.fill = glm::vec4((v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF) / 255.0f;
    }
    c.next_id = j.at("next_id");

    for (auto& p : j.at("ports"))
        c.ports.push_back(port_from_json(p));

    auto& sym = j.at("symbol");
    for (auto& s : sym.at("shapes"))
        c.symbol.shapes.push_back(shape_from_json(s));
    for (auto& p : sym.at("pins"))
        c.symbol.pins.push_back({p.at("id"), p.at("port"), vec(p.at("pos"))});

    auto& impl = j.at("impl");
    auto type = impl.at("type").get<std::string>();
    if (type == "schematic") {
        schematic s;
        for (auto& w : impl.at("wires"))
            s.wires.push_back({w.at("id"), vec(w.at("a")), vec(w.at("b"))});
        for (auto& i : impl.at("instances")) {
            instance r;
            r.id = i.at("id");
            r.cell = id_from_string(i.at("cell"));
            r.name = i.value("name", "");
            r.pos = vec(i.at("pos"));
            r.xf.rotation = i.value("rotation", 0);
            r.xf.mirror = i.value("mirror", false);
            if (auto it = i.find("params"); it != i.end()) {
                // anything else than the value types is dropped, the
                // default is used then
                for (auto& [name, v] : it->items())
                    if (auto x = param_value_from_json(v)) r.params[name] = *x;
            }
            s.instances.push_back(std::move(r));
        }
        c.impl = std::move(s);
    } else if (type == "builtin") {
        c.impl = builtin_impl{impl.at("kind")};
    } else if (type == "c_model") {
        c_model_impl m;
        m.plugin = impl.at("plugin");
        m.model = impl.at("model");
        m.params = params_from_json(impl.value("params", json::array()));
        groups_from_json(impl, m.groups, m.leaf_ports);
        c.impl = std::move(m);
    } else if (type == "hdl") {
        hdl_impl h;
        h.language = enum_from_string<hdl_language>(impl.at("language"), language_names, "hdl language");
        h.backend = enum_from_string<hdl_backend>(impl.at("backend"), backend_names, "hdl backend");
        h.sources = impl.value("sources", std::vector<std::string>{});
        h.top = impl.value("top", "");
        h.include_dirs = impl.value("include_dirs", std::vector<std::string>{});
        h.defines = impl.value("defines", std::vector<std::string>{});
        h.args = impl.value("args", "");
        h.params = params_from_json(impl.value("params", json::array()));
        groups_from_json(impl, h.groups, h.leaf_ports);
        c.impl = std::move(h);
    } else {
        // from a newer version, kept as it is
        c.impl = unknown_impl{type, impl.dump()};
    }

    return c;
}

json to_json(const bundle_type& b)
{
    json j;
    j["format"] = format_version;
    j["id"] = id_to_string(b.id);
    j["name"] = b.name;
    if (!b.description.empty()) j["description"] = b.description;
    j["fields"] = json::array();
    for (auto& f : b.fields) {
        json jf{{"name", f.name}};
        if (f.bundle != no_id) {
            jf["bundle"] = id_to_string(f.bundle);
            if (f.flipped) jf["flipped"] = true;
        } else {
            jf["dir"] = to_string(f.dir);
            jf["width"] = f.width;
        }
        if (!f.description.empty()) jf["description"] = f.description;
        j["fields"].push_back(std::move(jf));
    }
    return j;
}

bundle_type bundle_from_json(const json& j)
{
    int format = j.at("format");
    if (format > format_version)
        throw std::runtime_error("file format " + std::to_string(format) + " is newer than supported (" + std::to_string(format_version) + ")");

    bundle_type b;
    b.id = id_from_string(j.at("id"));
    b.name = j.at("name");
    b.description = j.value("description", "");
    for (auto& jf : j.at("fields")) {
        bundle_field f;
        f.name = jf.at("name");
        f.description = jf.value("description", "");
        if (jf.contains("bundle")) {
            f.bundle = id_from_string(jf.at("bundle"));
            f.flipped = jf.value("flipped", false);
        } else {
            f.dir = direction_from_string(jf.at("dir"));
            f.width = std::clamp(jf.value("width", 1), 1, max_width);
        }
        b.fields.push_back(std::move(f));
    }
    return b;
}

}
