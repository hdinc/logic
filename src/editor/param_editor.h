#pragma once

#include "../model/param.h"
#include "../model/workspace.h"

#include <filesystem>
#include <vector>

namespace logic::editor {

// imgui widgets for the parameters, returns true if a value changed. only
// values that differ from the defaults are kept in values. file parameters
// get a button for a file dialog, the chosen file is relative to base when
// it's inside it. with a workspace, bundle parameters get a list of the
// bundle types and field parameters a list of the fields of their bundle
// (if that parameter is among defs).
// the category of a cell: a text input and a list of the library's
// categories to pick from. returns true if it changed.
bool edit_category(const model::library& lib, std::string& category);

bool edit_params(const std::vector<model::param_def>& defs, model::param_values& values, const std::filesystem::path& base = {}, const model::workspace* ws = nullptr);
}
