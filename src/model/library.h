#pragma once

#include "cell.h"

#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace logic::model {

struct library {
    std::string name;

    // read only libraries (builtin) are defined in code and never saved
    bool read_only = false;

    // set when something in the library changed since the last save
    bool dirty = false;

    // folder of the library on disk, relative paths in its cells (e.g. hdl
    // sources) start here. empty for libraries that aren't saved.
    std::filesystem::path dir;

    std::vector<std::unique_ptr<cell>> cells;
    std::vector<std::unique_ptr<bundle_type>> bundles;

    // a read only library made from a plugin still lets its symbols be
    // edited: they're saved to this folder as cell files. empty otherwise.
    std::filesystem::path symbols_dir;
    // the symbols the cells have without a saved one, by cell
    std::map<id, model::symbol> default_symbols;
    // cells whose symbol was edited since the last save
    std::set<id> changed_symbols;
    // the categories of the cells, sorted
    std::vector<std::string> categories() const;

    cell* find_cell(id i);
    cell* find_cell(const std::string& name);
    bundle_type* find_bundle(id i);
    bundle_type* find_bundle(const std::string& name);

    std::string unique_cell_name(const std::string& prefix) const;
    std::string unique_bundle_name(const std::string& prefix) const;
};

}
