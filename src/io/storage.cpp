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

    return errors;
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

    lib.dirty = false;
}

}
