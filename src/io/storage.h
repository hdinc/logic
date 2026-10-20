#pragma once

#include "../model/workspace.h"

#include <filesystem>
#include <string>
#include <vector>

namespace logic::io {

// on disk a workspace is a folder with one subfolder per library:
//
//   <root>/<library>/library.json
//   <root>/<library>/cells/<cell name>.json
//
// loads every library in root into ws. problems with single files are
// returned as messages and the rest is still loaded.
std::vector<std::string> load_workspace(model::workspace& ws, const std::filesystem::path& root);

// writes the library and removes files of cells that no longer exist (deleted
// or renamed). throws std::runtime_error on failure.
void save_library(model::library& lib, const std::filesystem::path& root);

// writes the edited symbols of a read only library with a symbols folder
// (a plugin's) as cell files there. a symbol that is the default again
// removes its file. throws std::runtime_error on failure.
void save_symbols(model::library& lib);

}
