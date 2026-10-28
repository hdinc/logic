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
//   click an instance          select it, its state is in the properties
//   click a button / switch    press it / switch it
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

    // the camera of the cell's schematic in this view, e.g. to keep the one
    // of the editor when the simulation starts
    canvas& camera_of(const model::cell& c) { return editor_of(c).view; }

    // set when the user asks to open an instance in a new tab, the app
    // takes and clears it
    std::optional<std::vector<int>> open_in_new_tab;

private:
    void toolbar();
    void breadcrumb();
    void canvas_inputs(bool hovered, schematic_editor& e);
    void instance_menu();
    void signals_menu();
    // the value of a wide input of the top cell: text in its format, the
    // format, and the bits
    void port_value_popup();
    // node: a field of a bundle net (an index into its layout's nodes), -1
    // for the whole net
    void record_signal(int net, const std::string& name, int node = -1);
    // a menu item adding the net to the waveforms, for a bundle a menu of it
    // and its fields
    void signal_item(int net, const std::string& name, const char* label);
    // the bundle carried by the net in the shown scope
    std::optional<model::bundle_layout> bundle_of(int net) const;
    int pin_net(int instance, int port) const;
    // the state and the debugger of the selected instance
    void instance_panel();
    // selects an instance of the shown scope, -1 for none
    void select(int instance);

    const sim::scope& current() const;
    std::vector<int> path_of(const sim::scope& target) const;
    // the drawing editor of the cell, each cell keeps its own zoom and pan
    schematic_editor& editor_of(const model::cell& c);

    model::workspace& ws;
    render::renderer& r;
    std::shared_ptr<sim_session> s;
    std::vector<int> path;

    // by cell: the scopes of instances with other parameters have their own
    // copies of the cell, they live as long as the session
    std::map<const model::cell*, std::unique_ptr<schematic_editor>> editors;

    // instance under the right click menu
    int menu_instance = -1;
    int menu_net = -1;
    // the top cell's port whose value popup is open (or asked to open)
    int port_popup = -1;
    bool open_port_popup = false;
    std::string menu_signal_name;

    // selected in the shown scope, -1 for none
    int selected = -1;
    // a button held down with the mouse, -1 for none
    int pressed = -1;

    // debugger settings of the selected instance
    int memory = 0;
    uint64_t memory_address = 0;
    bool follow_pc = true;
    uint64_t disassemble_from = 0;
    uint64_t new_breakpoint = 0;
    std::string terminal_input;
    // what the selected axi test master is asked to do
    uint64_t axi_addr = 0;
    uint64_t axi_data = 0;
    int axi_len = 0;
    int axi_size = 63;
    void axi_master_panel(const sim::scope& sc);
};

}
