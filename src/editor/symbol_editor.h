#pragma once

#include "canvas.h"
#include "context.h"

#include <vector>

namespace logic::editor {

// edits the symbol of a cell: shapes and the pins that connect to its ports
//
//   left click/drag  select and move, drag on empty space for box select
//   shift            add to selection
//   p                place a pin, linked to the first port without a pin
//   l / b / c        draw a line / bezier (two ends, then control) / circle
//   t                place a text
//   delete           delete selection
//   g                toggle grid
class symbol_editor {
public:
    explicit symbol_editor(context ctx);

    // call every frame after view.set_mouse(). keys and clicks are handled only
    // when hovered, drags continue outside the editor area.
    void process_inputs(bool hovered);

    // draws into the currently bound framebuffer, after view.set_size()
    void draw();

    void property_panel();

    canvas view;

private:
    struct handle {
        enum kind_t {
            point,
            pin
        } kind;
        model::id id;
        int index = 0; // point index for shapes

        bool operator==(const handle&) const = default;
    };

    enum class mode_t {
        idle,
        moving,
        box_select,
        add_pin,
        add_shape,
    };

    // handles under the point, a shape's body yields all of its points
    std::vector<handle> hit_test(glm::vec2 p);
    std::vector<handle> handles_in(model::rect r);

    bool is_selected(handle h) const;
    bool is_shape_selected(model::id shape) const;
    void select(const std::vector<handle>& hs, bool add);

    void move_selection(glm::ivec2 d);
    void delete_selection();
    void validate_selection();

    void begin_shape(model::shape s);

    context ctx;
    model::symbol& sym;

    mode_t mode = mode_t::idle;
    std::vector<handle> selection;

    glm::vec2 drag_start{0, 0};
    glm::ivec2 drag_applied{0, 0};
    int drag_step = 1;

    glm::vec2 box_start{0, 0};

    model::shape draft;
    int draft_step = 0;
    model::pin draft_pin;
};

}
