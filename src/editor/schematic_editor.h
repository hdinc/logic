#pragma once

#include "canvas.h"
#include "context.h"
#include "tool.h"
#include "../sim/simulator.h"

#include <memory>
#include <optional>
#include <vector>

namespace logic::editor {

// edits the wires, instances and port connectors of a schematic cell
//
//   left click/drag  select and move, drag on empty space for box select
//   shift            add to selection, keep placing after a click
//   w                draw wires, click to add points, esc to end the wire
//   i / o            place an input / output port
//   space            place an instance (popup opened by the cell editor)
//   r                rotate selected instances or the one being placed
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
    };

    explicit schematic_editor(context ctx);

    // call every frame after view.set_mouse(). keys and clicks are handled only
    // when hovered, drags continue outside the editor area.
    void process_inputs(bool hovered);

    // draws into the currently bound framebuffer, after view.set_size()
    void draw();

    void property_panel();

    tool_kind current_tool() const { return kind; }
    // ignored while the current tool is busy
    void set_tool(tool_kind k);
    // starts placing instances of the cell
    void begin_place(model::id cell);

    // the current tool is in the middle of an operation, e.g. a drag
    bool busy() const { return active->busy(); }

    // after the cell was changed from outside, e.g. by undo
    void cell_replaced();

    canvas view;

    // while set, wires and ports show the simulated values of the scope and
    // the tools are not drawn. the scope must be an instance of this cell.
    const sim::simulator* sim = nullptr;
    const sim::scope* sim_scope = nullptr;

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
    std::vector<handle> handles_in(model::rect r);

    // box around everything selected, dragging inside it moves the selection
    std::optional<model::rect> selection_bounds() const;

    bool is_selected(handle h) const;
    void select(const std::vector<handle>& hs, bool add);

    void move_selection(glm::ivec2 d);
    void finish_move() { normalize(); }
    int drag_step() const { return model::grid; }
    void delete_selection();
    void rotate_selection();

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

    tool_kind kind = tool_kind::select;
    std::unique_ptr<tool<schematic_editor>> active;
};

}
