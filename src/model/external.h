#pragma once

#include "cell.h"

#include <filesystem>
#include <string>
#include <vector>

namespace logic::model {

// finds the interface of cells implemented outside the program (c models,
// hdl), e.g. by loading the plugin or running the hdl tool. implemented by
// the plugin loader and the hdl backends, registered in the workspace.
class external_provider {
public:
    virtual ~external_provider() = default;

    // true if it handles the cell's implementation
    virtual bool handles(const cell& c) const = 0;

    // ports and parameters of the model with the default parameter values.
    // the ids and positions of the ports don't matter. dir is the cell's
    // library folder, relative paths in the cell start there.
    virtual bool describe(const cell& c, const std::filesystem::path& dir, std::vector<port>& ports, std::vector<param_def>& params,
        std::string& error) const = 0;

    // ports for other parameter values, which are resolved
    virtual bool ports_for(const cell& c, const std::filesystem::path& dir, const param_values& values, std::vector<port>& ports,
        std::string& error) const = 0;
};

}
