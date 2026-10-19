#pragma once

#include "schematic_editor.h"
#include "sim_session.h"

#include <Hd/FrameBuffer.h>
#include <Hd/Texture.h>

#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace logic::editor {

// shows one instance (scope) of a running simulation: the schematic of its
// cell with the values of that instance.
//
//   double click an instance   show its schematic
//   right click an instance    open it here or in a new tab
//   backspace                  back to the parent
//   click an input port        toggle it (top cell only)
//   space                      step to the next event
class sim_view {
public:
    // path: instance indices from the top cell down to the shown scope
    sim_view(model::workspace& ws, render::renderer& r, std::shared_ptr<sim_session> s, std::vector<int> path = {});

    // controls, the path to the shown instance and its schematic in the
    // remaining space of the current imgui window
    void gui(Hd::Texture& tex, Hd::FrameBuffer& fb);

    void property_panel();
    // tree of all instances in the hierarchy, clicking one shows it
    void hierarchy_panel();

    sim_session& session() { return *s; }
    const std::shared_ptr<sim_session>& session_ptr() const { return s; }

    // e.g. "bar/U3 (foo)"
    std::string title() const;

    // set when the user asks to open an instance in a new tab, the app
    // takes and clears it
    std::optional<std::vector<int>> open_in_new_tab;

private:
    void toolbar();
    void breadcrumb();
    void canvas_inputs(bool hovered, schematic_editor& e);
    void instance_menu();

    const sim::scope& current() const;
    std::vector<int> path_of(const sim::scope& target) const;
    // the drawing editor of the cell, each cell keeps its own zoom and pan
    schematic_editor& editor_of(const model::cell& c);

    model::workspace& ws;
    render::renderer& r;
    std::shared_ptr<sim_session> s;
    std::vector<int> path;

    std::map<model::id, std::unique_ptr<schematic_editor>> editors;

    // instance under the right click menu
    int menu_instance = -1;
};

}
