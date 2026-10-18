#include "json.h"

#include <cstdio>
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
    } else if (auto c = std::get_if<circle>(&s.geom)) {
        j["type"] = "circle";
        j["center"] = vec(c->center);
        j["radius"] = c->radius;
    } else if (auto t = std::get_if<text>(&s.geom)) {
        j["type"] = "text";
        j["pos"] = vec(t->pos);
        j["text"] = t->str;
        j["size"] = t->size;
    }
    if (s.width != 0.0f) j["width"] = s.width;
    return j;
}

shape shape_from_json(const json& j)
{
    shape s;
    s.id = j.at("id");
    auto type = j.at("type").get<std::string>();
    if (type == "line") {
        s.geom = line{vec(j.at("a")), vec(j.at("b"))};
    } else if (type == "bezier") {
        s.geom = bezier{vec(j.at("a")), vec(j.at("b")), vec(j.at("c"))};
    } else if (type == "circle") {
        s.geom = circle{vec(j.at("center")), j.at("radius").get<float>()};
    } else if (type == "text") {
        s.geom = text{vec(j.at("pos")), j.at("text"), j.at("size").get<float>()};
    } else {
        throw std::runtime_error("invalid shape type '" + type + "'");
    }
    if (j.contains("width")) s.width = std::max(j.at("width").get<float>(), 0.0f);
    return s;
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

json to_json(const cell& c)
{
    json j;
    j["format"] = format_version;
    j["id"] = id_to_string(c.id);
    j["name"] = c.name;
    j["next_id"] = c.next_id;

    j["ports"] = json::array();
    for (auto& p : c.ports) {
        j["ports"].push_back({
            {"id", p.id},
            {"name", p.name},
            {"dir", to_string(p.dir)},
            {"width", p.width},
            {"pos", vec(p.pos)},
        });
    }

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
                    std::visit([&](auto& x) { params[name] = x; }, v);
            }
        }
    } else if (auto b = std::get_if<builtin_impl>(&c.impl)) {
        impl["type"] = "builtin";
        impl["kind"] = b->kind;
    }

    return j;
}

cell cell_from_json(const json& j)
{
    int format = j.at("format");
    if (format > format_version)
        throw std::runtime_error("file format " + std::to_string(format) + " is newer than supported (" + std::to_string(format_version) + ")");

    cell c;
    c.id = id_from_string(j.at("id"));
    c.name = j.at("name");
    c.next_id = j.at("next_id");

    for (auto& p : j.at("ports")) {
        port r;
        r.id = p.at("id");
        r.name = p.at("name");
        r.dir = direction_from_string(p.at("dir"));
        r.width = p.value("width", 1);
        r.pos = vec(p.at("pos"));
        c.ports.push_back(std::move(r));
    }

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
                for (auto& [name, v] : it->items()) {
                    if (v.is_boolean())
                        r.params[name] = v.get<bool>();
                    else if (v.is_number_integer())
                        r.params[name] = v.get<int64_t>();
                    else if (v.is_string())
                        r.params[name] = v.get<std::string>();
                    // anything else is dropped, the default is used then
                }
            }
            s.instances.push_back(std::move(r));
        }
        c.impl = std::move(s);
    } else if (type == "builtin") {
        c.impl = builtin_impl{impl.at("kind")};
    } else {
        throw std::runtime_error("invalid implementation type '" + type + "'");
    }

    return c;
}

}
