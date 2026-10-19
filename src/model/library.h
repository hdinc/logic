#pragma once

#include "cell.h"

#include <filesystem>
#include <memory>
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

    cell* find_cell(id i);
    cell* find_cell(const std::string& name);

    std::string unique_cell_name(const std::string& prefix) const;
};

}
