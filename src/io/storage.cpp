#include "storage.h"
#include "json.h"

#include <algorithm>
#include <fstream>
#include <set>

namespace logic::io {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

json read_json(const fs::path& p)
{
    std::ifstream f(p);
    if (!f) throw std::runtime_error("can't open " + p.string());
    return json::parse(f);
}

// writes to a temporary file first so a failed write never leaves a
// truncated file behind
void write_json(const fs::path& p, const json& j)
{
    auto tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp);
        if (!f) throw std::runtime_error("can't write " + tmp.string());
        f << j.dump(2) << '\n';
        if (!f) throw std::runtime_error("can't write " + tmp.string());
    }
    fs::rename(tmp, p);
}

}

std::vector<std::string> load_workspace(model::workspace& ws, const fs::path& root)
{
    std::vector<std::string> errors;

    if (!fs::is_directory(root)) return errors;

    std::vector<model::library*> loaded;

    for (auto& dir : fs::directory_iterator(root)) {
        auto lib_file = dir.path() / "library.json";
        if (!dir.is_directory() || !fs::exists(lib_file)) continue;

        std::string name;
        try {
            name = read_json(lib_file).at("name");
        } catch (std::exception& e) {
            errors.push_back(lib_file.string() + ": " + e.what());
            continue;
        }

        if (ws.find_library(name)) {
            errors.push_back(lib_file.string() + ": library '" + name + "' is already loaded");
            continue;
        }

        auto& lib = ws.add_library(name);
        lib.dir = dir.path();
        loaded.push_back(&lib);

        auto bundles_dir = dir.path() / "bundles";
        if (fs::is_directory(bundles_dir)) {
            for (auto& f : fs::directory_iterator(bundles_dir)) {
                if (f.path().extension() != ".json") continue;
                try {
                    auto b = bundle_from_json(read_json(f.path()));
                    if (ws.find_bundle(b.id)) throw std::runtime_error("duplicate bundle id " + id_to_string(b.id));
                    if (lib.find_bundle(b.name)) throw std::runtime_error("duplicate bundle name '" + b.name + "'");
                    lib.bundles.push_back(std::make_unique<model::bundle_type>(std::move(b)));
                } catch (std::exception& e) {
                    errors.push_back(f.path().string() + ": " + e.what());
                }
            }
            std::sort(lib.bundles.begin(), lib.bundles.end(), [](auto& a, auto& b) { return a->name < b->name; });
        }

        auto cells_dir = dir.path() / "cells";
        if (!fs::is_directory(cells_dir)) continue;

        for (auto& f : fs::directory_iterator(cells_dir)) {
            if (f.path().extension() != ".json") continue;
            try {
                auto c = cell_from_json(read_json(f.path()));
                if (ws.find_cell(c.id)) throw std::runtime_error("duplicate cell id " + id_to_string(c.id));
                if (lib.find_cell(c.name)) throw std::runtime_error("duplicate cell name '" + c.name + "'");
                lib.cells.push_back(std::make_unique<model::cell>(std::move(c)));
            } catch (std::exception& e) {
                errors.push_back(f.path().string() + ": " + e.what());
            }
        }

        // directory order is unspecified
        std::sort(lib.cells.begin(), lib.cells.end(), [](auto& a, auto& b) { return a->name < b->name; });
    }

    // the stored widths may be from before a type changed, the values of
    // expressions from before a cell they use changed. loading doesn't count
    // as a change.
    ws.bundles_changed();
    for (auto l : loaded)
        for (auto& c : l->cells)
            if (auto p = c->schematic_params(); p && !p->empty()) ws.apply_param_defaults(*c);
    for (auto l : loaded)
        l->dirty = false;

    return errors;
}

namespace {

// the same drawing and pins, whatever the ids
bool same_symbol(const model::symbol& a, const model::symbol& b)
{
    if (a.shapes.size() != b.shapes.size() || a.pins.size() != b.pins.size()) return false;
    for (size_t k = 0; k < a.shapes.size(); k++)
        if (a.shapes[k].geom != b.shapes[k].geom || a.shapes[k].width != b.shapes[k].width) return false;
    for (size_t k = 0; k < a.pins.size(); k++)
        if (a.pins[k].port != b.pins[k].port || a.pins[k].pos != b.pins[k].pos) return false;
    return true;
}

}

void save_symbols(model::library& lib)
{
    if (lib.symbols_dir.empty()) return;
    for (auto id : lib.changed_symbols) {
        auto c = lib.find_cell(id);
        if (!c) continue;
        auto p = lib.symbols_dir / (c->name + ".json");
        auto def = lib.default_symbols.find(id);
        if (def != lib.default_symbols.end() && same_symbol(def->second, c->symbol)) {
            std::error_code ec;
            fs::remove(p, ec);
            continue;
        }
        fs::create_directories(lib.symbols_dir);
        write_json(p, to_json(*c));
    }
    lib.changed_symbols.clear();
    lib.dirty = false;
}

void save_library(model::library& lib, const fs::path& root)
{
    if (lib.read_only) return;

    auto dir = root / lib.name;
    lib.dir = dir;
    auto cells_dir = dir / "cells";
    fs::create_directories(cells_dir);

    write_json(dir / "library.json", {{"format", format_version}, {"name", lib.name}});

    std::set<fs::path> written;
    for (auto& c : lib.cells) {
        auto p = cells_dir / (c->name + ".json");
        write_json(p, to_json(*c));
        written.insert(p);
    }

    // remove files of deleted or renamed cells
    for (auto& f : fs::directory_iterator(cells_dir)) {
        if (f.path().extension() == ".json" && !written.count(f.path()))
            fs::remove(f.path());
    }

    // the folder only exists while there are bundles
    auto bundles_dir = dir / "bundles";
    if (!lib.bundles.empty()) fs::create_directories(bundles_dir);
    for (auto& b : lib.bundles) {
        auto p = bundles_dir / (b->name + ".json");
        write_json(p, to_json(*b));
        written.insert(p);
    }
    if (fs::is_directory(bundles_dir)) {
        for (auto& f : fs::directory_iterator(bundles_dir)) {
            if (f.path().extension() == ".json" && !written.count(f.path()))
                fs::remove(f.path());
        }
        std::error_code ec;
        if (lib.bundles.empty()) fs::remove(bundles_dir, ec);
    }

    lib.dirty = false;
}

}
