#include "libraries.h"
#include "../io/json.h"

#include <algorithm>
#include <cstddef>
#include <fstream>

namespace logic::plugin {

namespace fs = std::filesystem;

namespace {

#define HAS_FIELD(d, field) ((d).struct_size >= offsetof(logic_model_desc, field) + sizeof((d).field) && (d).field)

// the symbol in json text for the cell, false with an error
bool parse_symbol(model::cell& c, const std::string& text, model::symbol& out, std::string& error)
{
    try {
        out = io::symbol_for(c, nlohmann::json::parse(text));
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

// the cell's symbol: saved > the plugin's > generated. the default (without
// a saved one) goes to the library.
void choose_symbol(model::library& lib, model::cell& c, const logic_model_desc& d, const std::string& plugin, std::vector<std::string>& messages)
{
    std::string error;
    model::symbol def;
    bool have_def = false;
    if (HAS_FIELD(d, symbol)) {
        if (parse_symbol(c, d.symbol, def, error)) have_def = true;
        else messages.push_back("the symbol of " + plugin + "/" + c.name + " in the plugin is broken: " + error);
    }
    if (!have_def) {
        c.generate_symbol(model::pin_order::declaration);
        def = c.symbol;
    }
    lib.default_symbols[c.id] = def;
    c.symbol = def;

    auto file = lib.symbols_dir / (c.name + ".json");
    std::error_code ec;
    if (!fs::exists(file, ec)) return;
    std::ifstream in(file);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    model::symbol saved;
    if (parse_symbol(c, text, saved, error)) c.symbol = std::move(saved);
    else messages.push_back(file.string() + " doesn't fit the model, using the default symbol: " + error);
}

// the id of a bundle type declared by a plugin, the same in every run
model::id plugin_bundle_id(const std::string& plugin, const std::string& name)
{
    return model_cell_id(plugin, "bundle/" + name);
}

// the bundle types (into the plugin's library) and port groups of a model's
// bundles json, false with an error
bool parse_bundles(model::library& lib, const std::string& plugin, const std::string& text, std::vector<model::port_group>& groups, std::string& error)
{
    try {
        auto j = nlohmann::json::parse(text);
        auto dir = [](const std::string& s) {
            if (s == "input") return model::direction::input;
            if (s == "output") return model::direction::output;
            if (s == "inout") return model::direction::inout;
            throw std::runtime_error("invalid direction '" + s + "'");
        };
        for (auto& jt : j.value("types", nlohmann::json::array())) {
            std::string name = jt.at("name");
            auto id = plugin_bundle_id(plugin, name);
            auto b = lib.find_bundle(id);
            if (!b) {
                lib.bundles.push_back(std::make_unique<model::bundle_type>());
                b = lib.bundles.back().get();
                b->id = id;
            }
            b->name = name;
            b->description = jt.value("description", "");
            b->fields.clear();
            for (auto& jf : jt.at("fields")) {
                model::bundle_field f;
                f.name = jf.at("name");
                f.description = jf.value("description", "");
                if (jf.contains("type")) {
                    f.bundle = plugin_bundle_id(plugin, jf.at("type"));
                    f.flipped = jf.value("flipped", false);
                } else {
                    f.dir = dir(jf.value("dir", "output"));
                    f.width = std::clamp(jf.value("width", 1), 1, model::max_width);
                }
                b->fields.push_back(std::move(f));
            }
        }
        for (auto& jp : j.value("ports", nlohmann::json::array())) {
            model::port_group g;
            g.name = jp.at("name");
            std::string type = jp.at("type");
            g.bundle = plugin_bundle_id(plugin, type);
            if (!lib.find_bundle(g.bundle)) throw std::runtime_error("port " + g.name + ": no bundle type '" + type + "'");
            g.flipped = jp.value("flipped", false);
            g.prefix = jp.value("prefix", "");
            g.side = jp.value("side", "right") == "left" ? model::direction::input : model::direction::output;
            g.description = jp.value("description", "");
            groups.push_back(std::move(g));
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

}

model::id model_cell_id(const std::string& plugin, const std::string& model)
{
    // 64 bit fnv-1a, like builtin_cell_id
    uint64_t h = 0xcbf29ce484222325ull;
    for (char ch : "plugin/" + plugin + "/" + model) {
        h ^= (unsigned char)ch;
        h *= 0x100000001b3ull;
    }
    return h == model::no_id ? 1 : h;
}

std::vector<std::string> sync_libraries(model::workspace& ws, const registry& reg, const fs::path& symbols_root)
{
    std::vector<std::string> messages;
    for (auto& a : reg.scan()) {
        auto lib = ws.find_library(a.name);
        if (lib && lib->symbols_dir.empty()) {
            messages.push_back("plugin " + a.name + " isn't shown, a library has the same name");
            continue;
        }

        std::string error;
        auto p = reg.load(a.name, {}, error);
        if (!p) {
            messages.push_back("plugin " + a.name + ": " + error);
            continue;
        }

        if (!lib) {
            lib = &ws.add_library(a.name);
            lib->read_only = true;
        }
        lib->dir = a.path.parent_path();
        lib->symbols_dir = symbols_root / a.name / "symbols";

        for (uint32_t k = 0; k < p->info->model_count; k++) {
            auto& d = p->info->models[k];
            auto id = model_cell_id(a.name, d.name);
            auto c = lib->find_cell(id);
            if (!c) {
                auto n = std::make_unique<model::cell>();
                n->id = id;
                n->name = d.name;
                n->impl = model::c_model_impl{a.name, d.name, {}, {}, {}};
                lib->cells.push_back(std::move(n));
                c = lib->cells.back().get();
            }
            std::vector<model::port> ports;
            std::vector<model::param_def> params;
            if (!reg.describe(*c, lib->dir, ports, params, error)) {
                messages.push_back(a.name + "/" + d.name + ": " + error);
                continue;
            }
            // bundle ports made of the model's ports
            auto& impl = std::get<model::c_model_impl>(c->impl);
            impl.groups.clear();
            impl.leaf_ports.clear();
            if (HAS_FIELD(d, bundles) && !parse_bundles(*lib, a.name, d.bundles, impl.groups, error))
                messages.push_back("the bundles of " + a.name + "/" + d.name + " are broken: " + error);
            if (impl.groups.empty()) {
                c->set_ports(ports);
            } else {
                impl.leaf_ports = std::move(ports);
                for (auto& p : ws.apply_groups(*c))
                    messages.push_back(a.name + "/" + d.name + ": " + p);
            }
            impl.params = std::move(params);
            c->category = d.category ? d.category : "";
            choose_symbol(*lib, *c, d, a.name, messages);
        }
        // nothing to save from building it
        lib->dirty = false;
        lib->changed_symbols.clear();
    }
    // the plugins' bundle types may have changed
    ws.bundles_changed();
    for (auto& l : ws.libraries)
        if (!l->symbols_dir.empty()) l->dirty = false;
    return messages;
}

}
