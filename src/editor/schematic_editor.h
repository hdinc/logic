#pragma once

#include "canvas.h"
#include "context.h"

#include <optional>
#include <vector>

namespace logic::editor {

// edits the wires, instances and port connectors of a schematic cell
//
//   left click/drag  select and move, drag on empty space for box select
//   shift            add to selection
//   w                draw wires, click to add points, esc to finish
//   i / o            place an input / output port
//   space            place an instance (popup opened by the app)
//   r                rotate selected instances or the one being placed
//   delete           delete selection
//   g                toggle grid
class schematic_editor {
public:
    explicit schematic_editor(context ctx);

    // call every frame after view.set_mouse(). keys and clicks are handled only
    // when hovered, drags continue outside the editor area.
    void process_inputs(bool hovered);

    // draws into the currently bound framebuffer, after view.set_size()
    void draw();

    void property_panel();

    // starts placing an instance of the cell, follows the mouse until clicked
    void begin_place(model::id cell);

    bool is_idle() const { return mode == mode_t::idle; }

    canvas view;

private:
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

    enum class mode_t {
        idle,
        moving,
        box_select,
        place_instance,
        place_port,
        wire,
    };

    // handles under the point, a wire segment yields both of its endpoints
    std::vector<handle> hit_test(glm::vec2 p);
    std::vector<handle> handles_in(model::rect r);

    // box around everything selected, dragging inside it moves the selection
    std::optional<model::rect> selection_bounds() const;

    bool is_selected(handle h) const;
    void select(const std::vector<handle>& hs, bool add);

    void move_selection(glm::ivec2 d);
    void delete_selection();
    void rotate_selection();

    // merges and splits wires so every junction is an endpoint
    void normalize();

    // removes handles of elements that no longer exist
    void validate_selection();

    std::string unique_instance_name() const;

    context ctx;
    model::schematic& sch;

    mode_t mode = mode_t::idle;
    std::vector<handle> selection;

    glm::vec2 drag_start{0, 0};
    glm::ivec2 drag_applied{0, 0};
    bool drag_moved = false;
    // drag started on empty space inside the selection bounds
    bool drag_from_bounds = false;

    glm::vec2 box_start{0, 0};

    model::instance placing;
    model::port placing_port;

    bool wire_started = false;
    glm::ivec2 wire_start{0, 0};
};

}
