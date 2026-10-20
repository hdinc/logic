#pragma once

#include "canvas.h"
#include "context.h"
#include "tool.h"

#include <memory>
#include <optional>
#include <vector>

namespace logic::editor {

// edits the symbol of a cell: shapes and the pins that connect to its ports
//
//   left click/drag  select and move, drag on empty space for box select
//   shift            add to selection, keep drawing after a click
//   p                place a pin, linked to the first port without a pin
//   l / b / c        draw a line / bezier (two ends, then control) / circle
//   a                draw an arc (two ends, then a point on it)
//   t                place a text
//   ctrl+c/x/v       copy, cut, paste shapes (handled by the app)
//   delete           delete selection
//   esc              cancel the tool / clear selection
//   g                toggle grid
class symbol_editor {
public:
    enum class tool_kind {
        select,
        pin,
        line,
        bezier,
        arc,
        circle,
        text,
        paste,
    };

    explicit symbol_editor(context ctx);

    // call every frame after view.set_mouse(). keys and clicks are handled only
    // when hovered, drags continue outside the editor area.
    void process_inputs(bool hovered);

    // draws into the currently bound framebuffer, after view.begin_frame()
    void draw();

    void property_panel();
    // the common properties of several selected shapes and pins
    void selection_properties();

    tool_kind current_tool() const { return kind; }
    // ignored while the current tool is busy
    void set_tool(tool_kind k);

    // the current tool is in the middle of an operation, e.g. a drag
    bool busy() const { return active->busy(); }

    // shapes with a selected point in a cell of their own, moved so the
    // middle of the selection is at the origin. pins belong to their port
    // and are not copied. empty if no shape is selected.
    std::optional<model::cell> copy_selection() const;
    // the copied shapes follow the mouse, a click places them (shift keeps
    // placing)
    void begin_paste(model::cell part);
    // adds the shapes moved by offset with new ids and selects them
    void insert(const model::symbol& part, glm::ivec2 offset);

    // after the cell was changed from outside, e.g. by undo
    void cell_replaced() { validate_selection(); }

    canvas view;

    // used by the tools

    struct handle {
        enum kind_t {
            point,
            pin
        } kind;
        model::id id;
        int index = 0; // point index for shapes

        bool operator==(const handle&) const = default;
    };

    // handles under the point, a shape's body yields all of its points
    std::vector<handle> hit_test(glm::vec2 p);
    std::vector<handle> handles_in(model::rect r);

    // box around everything selected, dragging inside it moves the selection
    std::optional<model::rect> selection_bounds() const;

    bool is_selected(handle h) const;
    bool is_shape_selected(model::id shape) const;
    void select(const std::vector<handle>& hs, bool add);

    void move_selection(glm::ivec2 d);
    void finish_move() {}
    // pins live on the schematic grid, shapes on single units
    int drag_step() const;
    // see transform_shortcut, about the middle of the selection
    void transform_selection(int turns, bool mirror);
    void delete_selection();
    void validate_selection();

    // draws a pin with the name of its port
    void draw_pin(const model::pin& p, glm::vec4 color);

    context ctx;
    model::symbol& sym;

    std::vector<handle> selection;

private:
    tool_kind kind = tool_kind::select;
    std::unique_ptr<tool<symbol_editor>> active;
};

}
