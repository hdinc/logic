#pragma once

#include "context.h"

#include <imgui.h>

namespace logic::editor {

// turning and mirroring a selection as a whole: ctrl+r a quarter turn
// counter clockwise, ctrl+shift+r clockwise, ctrl+m mirrors left and right.
// false if none of them was pressed.
inline bool transform_shortcut(int& turns, bool& mirror)
{
    auto& io = ImGui::GetIO();
    if (!io.KeyCtrl) return false;
    turns = 0;
    mirror = false;
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) turns = io.KeyShift ? -1 : 1;
    else if (ImGui::IsKeyPressed(ImGuiKey_M, false)) mirror = true;
    else return false;
    return true;
}

// a point of the selection after transform_shortcut's change: mirrored
// first, then turned, about the pivot
inline glm::ivec2 turn_about(glm::ivec2 p, glm::ivec2 pivot, int turns, bool mirror)
{
    return pivot + model::transform{turns, mirror}.apply(p - pivot);
}

// the pivot for a selection: the grid point nearest to its middle, so
// points on the grid stay on it
inline glm::ivec2 pivot_of(const model::rect& r) { return model::snap((r.min + r.max) * 0.5f, model::grid); }

// what the mouse does in an editor: select, draw wires, place a port, ... the
// editor has one active tool and goes back to its select tool when a tool is
// done.
template <class Editor>
class tool {
public:
    virtual ~tool() = default;

    // called every frame. keys and clicks are handled only when hovered, drags
    // continue outside the editor area.
    virtual void update(Editor& e, bool hovered) = 0;

    // previews, drawn over the edited cell
    virtual void draw(Editor&) {}

    // in the middle of an operation, e.g. a drag. the tool can't be switched
    // and no undo step is recorded meanwhile.
    virtual bool busy() const { return false; }

    // set by the tool when it finished
    bool done = false;
};

// click to select, drag to move, drag on empty space for a box selection,
// shift adds to the selection. dragging inside the box around a multiple
// selection moves it as well.
//
// the editor provides the selection and these functions:
//   hit_test(p), handles_in(rect), selection_bounds(), is_selected(handle),
//   select(handles, add), move_selection(d), finish_move(), drag_step(),
//   delete_selection()
template <class Editor>
class select_tool : public tool<Editor> {
public:
    void update(Editor& e, bool hovered) override
    {
        using namespace ImGui;

        auto m = e.view.mouse_world();
        bool shift = GetIO().KeyShift;

        switch (state) {
        case state_t::idle: {
            if (!hovered) break;

            if (IsKeyPressed(ImGuiKey_Delete)) {
                e.delete_selection();
            } else if (IsKeyPressed(ImGuiKey_Escape)) {
                e.selection.clear();
            } else if (IsMouseClicked(ImGuiMouseButton_Left)) {
                auto hs = e.hit_test(m);
                auto bounds = e.selection_bounds();
                bool in_bounds = hs.empty() && !shift && bounds && bounds->contains(m);
                if (hs.empty() && !in_bounds) {
                    if (!shift) e.selection.clear();
                    box_start = m;
                    state = state_t::box_select;
                } else {
                    if (!hs.empty() && !e.is_selected(hs[0])) e.select(hs, shift);
                    drag_start = m;
                    drag_applied = {0, 0};
                    drag_moved = false;
                    drag_from_bounds = in_bounds;
                    drag_step = e.drag_step();
                    state = state_t::moving;
                }
            }
        } break;

        case state_t::moving: {
            auto d = model::snap(m - drag_start, drag_step) - drag_applied;
            if (d != glm::ivec2{0, 0}) {
                e.move_selection(d);
                drag_applied += d;
                drag_moved = true;
            }
            if (!IsMouseDown(ImGuiMouseButton_Left)) {
                if (drag_moved) e.finish_move();
                // a plain click on empty space still clears the selection
                else if (drag_from_bounds) e.selection.clear();
                state = state_t::idle;
            }
        } break;

        case state_t::box_select: {
            if (!IsMouseDown(ImGuiMouseButton_Left)) {
                e.select(e.handles_in(model::make_rect(box_start, m)), true);
                state = state_t::idle;
            }
        } break;
        }
    }

    void draw(Editor& e) override
    {
        auto& r = e.ctx.r;
        auto c = render::colors().selection;

        if (state == state_t::box_select) {
            r.rect(box_start, e.view.mouse_world(), {c.r, c.g, c.b, 0.12f});
            r.rect_outline(box_start, e.view.mouse_world(), c, r.pixel());
        } else if (e.selection.size() > 1) {
            if (auto b = e.selection_bounds()) r.rect_outline(b->min, b->max, {c.r, c.g, c.b, 0.6f}, r.pixel(), 2.0f);
        }
    }

    // only while a drag moves things: a click or a box selection doesn't
    // grey out the rest of the editor
    bool busy() const override { return state == state_t::moving && drag_moved; }

private:
    enum class state_t {
        idle,
        moving,
        box_select,
    } state = state_t::idle;

    glm::vec2 drag_start{0, 0};
    glm::ivec2 drag_applied{0, 0};
    int drag_step = 1;
    bool drag_moved = false;
    // drag started on empty space inside the selection bounds
    bool drag_from_bounds = false;

    glm::vec2 box_start{0, 0};
};

}
