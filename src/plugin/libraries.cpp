#include "libraries.h"
#include "../io/json.h"

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
                n->impl = model::c_model_impl{a.name, d.name, {}};
                lib->cells.push_back(std::move(n));
                c = lib->cells.back().get();
            }
            std::vector<model::port> ports;
            std::vector<model::param_def> params;
            if (!reg.describe(*c, lib->dir, ports, params, error)) {
                messages.push_back(a.name + "/" + d.name + ": " + error);
                continue;
            }
            c->set_ports(ports);
            std::get<model::c_model_impl>(c->impl).params = std::move(params);
            lib->categories[id] = d.category ? d.category : "";
            choose_symbol(*lib, *c, d, a.name, messages);
        }
        // nothing to save from building it
        lib->dirty = false;
        lib->changed_symbols.clear();
    }
    return messages;
}

}
