// plugins as read only libraries: one per plugin, a cell per model

#pragma once

#include "registry.h"

#include <filesystem>
#include <string>
#include <vector>

namespace logic::plugin {

// the id of a model's cell, the same in every run
model::id model_cell_id(const std::string& plugin, const std::string& model);

// adds a read only library for every plugin in the registry's search dirs,
// or updates the one added before (cells keep their ids, models that are
// gone keep their cells). the symbol of a model is the cell file
// symbols_root/<plugin>/symbols/<model>.json if there is one, else the
// plugin's, else generated. returns messages for the user, e.g. plugins
// that couldn't be loaded.
std::vector<std::string> sync_libraries(model::workspace& ws, const registry& reg, const std::filesystem::path& symbols_root);

}
