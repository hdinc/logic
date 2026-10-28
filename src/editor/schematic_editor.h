#pragma once

#include "canvas.h"
#include "context.h"
#include "tool.h"
#include "../netlist/netlist.h"
#include "../netlist/port_setup.h"
#include "../sim/simulator.h"

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace logic::editor {

// edits the wires, instances and port connectors of a schematic cell
//
//   left click/drag  select and move, drag on empty space for box select
//   ctrl+click       add to selection (again: remove), ctrl+drag: box adds
//   shift+click      remove from selection, shift+drag: box removes
//   shift            keep placing after a click
//   w                draw wires, click to add points, esc to end the wire
//   i / o            place an input / output port
//   space            place an instance (popup opened by the cell editor)
//   ctrl+c/x/v       copy, cut, paste (handled by the app)
//   ctrl+r / ctrl+m  rotate / mirror the selection (shift: the other way)
//   r / m            rotate / mirror what is being placed or pasted
//   q                duplicate the element under the mouse and place it
//   delete           delete selection
//   esc              cancel the tool / clear selection
//   g                toggle grid
class schematic_editor {
public:
    enum class tool_kind {
        select,
        wire,
        input_port,
        output_port,
        place_instance,
        paste,
    };

    explicit schematic_editor(context ctx);

    // call every frame after view.set_mouse(). keys and clicks are handled only
    // when hovered, drags continue outside the editor area.
    void process_inputs(bool hovered);

    // draws into the currently bound framebuffer, after view.begin_frame()
    void draw();

    // the pin or port connector under the mouse (name, direction, width,
    // description) or the width of the wire, with the simulated values.
    // shown after the mouse stayed on it for tooltip_delay, at a fixed
    // place. call while the canvas is hovered.
    void hover_tooltip();
    static constexpr double tooltip_delay = 1.0;

    void property_panel();

    tool_kind current_tool() const { return kind; }
    // ignored while the current tool is busy
    void set_tool(tool_kind k);
    // starts placing instances of the cell
    void begin_place(model::id cell);
    // the cell being placed, no_id if not placing
    model::id placing() const { return kind == tool_kind::place_instance ? placing_cell : model::no_id; }

    // the selected ports, instances and wires (with both ends selected) in a
    // cell of their own, moved so the middle of the selection is at the
    // origin. empty if nothing is selected.
    std::optional<model::cell> copy_selection() const;
    // the copied elements follow the mouse, a click places them (shift keeps
    // placing), r rotates and m mirrors them. instances that would contain
    // this cell are left out.
    void begin_paste(model::cell part);
    // starts pasting a copy of the instance, port or wire under the mouse
    void duplicate_hovered();
    // adds the copied elements moved by offset with new ids and selects them
    void insert(const model::cell& part, glm::ivec2 offset);
    // a wired port for each unconnected pin of the instance, see
    // netlist::add_ports_for_pins. the new ports are selected.
    void add_ports_for_pins(model::id instance);

    // the current tool is in the middle of an operation, e.g. a drag
    bool busy() const { return active->busy(); }

    // after the cell was changed from outside, e.g. by undo
    void cell_replaced();

    canvas view;

    // while set, wires and ports show the simulated values of the scope and
    // the tools are not drawn. the scope must be an instance of this cell.
    const sim::snapshot* sim = nullptr;
    const sim::scope* sim_scope = nullptr;
    // highlighted while simulating, -1 for none
    int sim_selected = -1;
    // a net of this cell's netlist (the scope's while simulating) drawn
    // highlighted: its wires, ports and tunnels. -1 for none.
    int highlight_net = -1;
    // the net of what is under the point: a port connector, a pin, a
    // tunnel or a wire. -1 if there is none.
    int net_at(glm::vec2 p) const;

    // what displays and inputs show while simulating, instance `index` of
    // the schematic. while editing: the dark segments of 7-segment displays
    // (also for previews of tools).
    void draw_live(const model::instance& i, size_t index);

    // used by the tools

    struct handle {
        enum kind_t {
            port,
            instance,
            wire_a,
            wire_b
        } kind;
        model::id id;

        bool operator==(const handle&) const = default;
    };

    // handles under the point, a wire segment yields both of its endpoints
    std::vector<handle> hit_test(glm::vec2 p);
    // no_id if there is no wire
    model::id wire_at(glm::vec2 p) const;
    std::vector<handle> handles_in(model::rect r);

    // box around everything selected, dragging inside it moves the selection
    std::optional<model::rect> selection_bounds() const;

    bool is_selected(handle h) const;
    void select(const std::vector<handle>& hs, bool add);
    // the elements in a cell of their own (wires only with both ends),
    // moved so origin is at 0
    std::optional<model::cell> copy_of(const std::vector<handle>& handles, glm::ivec2 origin) const;

    void move_selection(glm::ivec2 d);
    void finish_move() { normalize(); }
    int drag_step() const { return model::grid; }
    void delete_selection();
    // see transform_shortcut. a single instance or port turns about its origin,
    // more elements about the middle of the selection.
    // the common properties of several selected instances and ports
    void selection_properties(const std::vector<handle>& elements);
    void transform_selection(int turns, bool mirror);

    // merges and splits wires so every junction is an endpoint
    void normalize();

    // removes handles of elements that no longer exist
    void validate_selection();

    std::string unique_instance_name() const;

    context ctx;
    model::schematic& sch;

    std::vector<handle> selection;

private:
    void set_tool(tool_kind k, std::unique_ptr<tool<schematic_editor>> t);

    // the segments of a display stay lit for its persistence time after they
    // were last lit, by scope and instance index. cleared when not
    // simulating.
    struct ghost {
        std::chrono::steady_clock::time_point lit_until[8];
        bool shown[8] = {};
    };
    std::map<std::pair<const sim::scope*, size_t>, ghost> ghosts;

    // what is under the mouse for the tooltip, empty key if nothing
    struct hover_target {
        std::string key;
        std::function<void()> show;
    };
    hover_target hover_at(glm::vec2 p) const;

    struct {
        std::string key;
        double since = 0.0;
        std::optional<ImVec2> pos;
    } hover;

    // connectivity for drawing buses and width errors while editing,
    // extracted again every frame
    netlist::netlist nets;
    // problems with the expressions of the cell, see update_param_values
    std::vector<std::string> param_problems;
    // evaluates the expressions of the cell for its default parameter values
    void update_param_values();
    // the pins each port was connected to in the last frame, so normalize
    // can tell which ports were just connected (see adopt_pin_settings)
    std::map<model::id, std::set<std::pair<model::id, int>>> linked;
    // the instance under the right click menu
    model::id menu_instance = model::no_id;

    tool_kind kind = tool_kind::select;
    std::unique_ptr<tool<schematic_editor>> active;
    model::id placing_cell = model::no_id;
};

}
