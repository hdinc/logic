#pragma once

#include "../model/param.h"

#include <filesystem>
#include <vector>

namespace logic::editor {

// imgui widgets for the parameters, returns true if a value changed. only
// values that differ from the defaults are kept in values. file parameters
// get a button for a file dialog, the chosen file is relative to base when
// it's inside it.
bool edit_params(const std::vector<model::param_def>& defs, model::param_values& values, const std::filesystem::path& base = {});

}
