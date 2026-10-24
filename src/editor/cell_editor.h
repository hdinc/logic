#pragma once

#include "model_editor.h"
#include "schematic_editor.h"
#include "sim_view.h"
#include "../model/history.h"

#include <Hd/FrameBuffer.h>
#include <Hd/Texture.h>

#include <imgui.h>

#include <memory>
#include <optional>

namespace logic::editor {

// the schematic of one cell. f5 simulates the cell: the editor then shows a
// sim_view of it and the cell can't be edited. cells implemented outside the
// program (c models, hdl) have a model page instead of the schematic. the
// symbol is edited in a symbol_page, which shares the undo history.
class cell_editor {
public:
    cell_editor(context ctx, std::shared_ptr<model::history> hist);
    // stops the simulation, views of it in other tabs close
    ~cell_editor() { stop_simulation(); }

    model::cell& cell() { return ctx.cell; }
    model::library& library() { return ctx.lib; }

    // draws the editor into the remaining space of the current imgui window.
    // tex/fb are shared by all editors, only the visible one is drawn.
    void gui(Hd::Texture& tex, Hd::FrameBuffer& fb);

    void property_panel();

    bool simulating() const { return session != nullptr; }
    // ignored while a tool is busy. hdl models that aren't built are built
    // first, the simulation starts when they are done.
    void start_simulation();
    void stop_simulation();

    // the simulation of this cell, null if not simulating
    const std::shared_ptr<sim_session>& simulation() const { return session; }
    sim_view* simulation_view() { return view.get(); }

    // the cell is part of another cell's running simulation and can't be
    // edited. empty if not locked, otherwise the name of that cell.
    void set_locked_by(std::string top) { locked_by = std::move(top); }
    bool locked() const { return !locked_by.empty(); }

    // not while simulating or locked. undo and redo are also ignored while
    // a tool is busy (e.g. during a drag).
    bool can_undo() const;
    bool can_redo() const;
    void undo();
    void redo();

    // the selection of the schematic through the system clipboard. pasting
    // starts placing the copy at the mouse.
    bool can_copy() const;
    bool can_paste() const;
    void copy();
    void cut();
    void paste();

    // placing instances from outside, e.g. the component bar. can't while
    // simulating, locked, or if the cell would contain itself. place is also
    // ignored while a tool is busy.
    bool can_place(model::id cell) const;
    void place(model::id cell);
    // the cell being placed in the schematic, no_id if none
    model::id placing() const { return session || !schematic ? model::no_id : schematic->placing(); }
    // stops placing, back to the select tool
    void cancel_place();

    // after the cell was changed from outside, e.g. by an undo in its
    // symbol page
    void cell_replaced();

    // screen rectangle of the schematic canvas drawn by the last gui(),
    // while things can be placed on it (not simulating or locked). the
    // component bar is drawn over it.
    struct screen_rect {
        ImVec2 min, max;
    };
    std::optional<screen_rect> placing_canvas() const { return canvas_rect; }

private:
    std::optional<screen_rect> canvas_rect;

    void toolbar();
    void edit_toolbar();
    void place_popup();
    void open_place_popup();
    bool busy() const { return schematic && schematic->busy(); }

    context ctx;
    std::shared_ptr<model::history> hist;
    size_t seen_restores = 0;
    // null for cells without a schematic, they have the model page
    std::unique_ptr<schematic_editor> schematic;
    std::unique_ptr<model_editor> model_page;

    // the place popup: its search text, the entry chosen with the keys
    bool focus_search = false;
    std::string search;
    std::string last_search;
    int selected = -1;
    bool reset_selection = false;
    bool scroll_to_selected = false;

    std::shared_ptr<sim_session> session;
    std::unique_ptr<sim_view> view;

    // waiting for these hdl cells to be built before simulating
    std::vector<model::id> building;
    // why the last start failed
    std::string start_error;

    std::string locked_by;
};

}
