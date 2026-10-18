#pragma once

#include "../model/param.h"

#include <vector>

namespace logic::editor {

// imgui widgets for the parameters, returns true if a value changed. only
// values that differ from the defaults are kept in values.
bool edit_params(const std::vector<model::param_def>& defs, model::param_values& values);

}
