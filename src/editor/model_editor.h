#pragma once

#include "context.h"

#include <string>

namespace logic::editor {

// settings and interface of a cell implemented outside the program: the
// plugin and model of a c model, or the sources, top and tool of an hdl
// cell, and the ports and parameters found in it. drawn as a page instead of
// a schematic.
class model_editor {
public:
    explicit model_editor(context ctx);

    // into the remaining space of the current imgui window
    void gui(bool locked);

private:
    void c_model_settings(model::c_model_impl& m);
    void hdl_settings(model::hdl_impl& h);
    // build button, tool and status of the last build
    void hdl_build(const model::hdl_impl& h);
    void ports_table();
    // bundle ports made of the model's ports
    // inputs for the values of the group's bundle parameters, true if
    // changed
    bool group_args(model::port_group& g, const model::param_values& defaults);
    void groups_table();
    // read only ports and parameters of a plugin's model
    void plugin_tables();
    void params_table();

    context ctx;
    // result of the last refresh
    std::string refresh_message;
    bool refresh_failed = false;
};

}
