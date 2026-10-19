#pragma once

#include "model_editor.h"
#include "schematic_editor.h"
#include "sim_view.h"
#include "symbol_editor.h"
#include "../model/history.h"

#include <Hd/FrameBuffer.h>
#include <Hd/Texture.h>

namespace logic::editor {

// schematic and symbol view of one cell, switched with tab. f5 simulates the
// cell: the editor then shows a sim_view of it and the cell can't be edited.
// cells implemented outside the program (c models, hdl) have a model page
// instead of the schematic.
class cell_editor {
public:
    explicit cell_editor(context ctx);
    // stops the simulation, views of it in other tabs close
    ~cell_editor() { stop_simulation(); }

    model::cell& cell() { return ctx.cell; }
    model::library& library() { return ctx.lib; }

    // draws the editor into the remaining space of the current imgui window.
    // tex/fb are shared by all editors, only the visible one is drawn.
    void gui(Hd::Texture& tex, Hd::FrameBuffer& fb);

    void property_panel();

    bool simulating() const { return session != nullptr; }
    // ignored while a tool is busy
    void start_simulation();
    void stop_simulation();

    // the simulation of this cell, null if not simulating
    const std::shared_ptr<sim_session>& simulation() const { return session; }
    sim_view* simulation_view() { return view.get(); }

    // the cell is part of another cell's running simulation and can't be
    // edited. empty if not locked, otherwise the name of that cell.
    void set_locked_by(std::string top) { locked_by = std::move(top); }
    bool locked() const { return !locked_by.empty(); }

    // ignored while a tool is busy (e.g. during a drag), simulating or locked
    bool can_undo() const;
    bool can_redo() const;
    void undo();
    void redo();

    // the selection of the shown view (schematic or symbol) through the
    // system clipboard. pasting starts placing the copy at the mouse.
    bool can_copy() const;
    bool can_paste() const;
    void copy();
    void cut();
    void paste();

    // placing instances from outside, e.g. the component bar. switches to
    // the schematic. can't while simulating, locked or busy, or if the cell
    // would contain itself.
    bool can_place(model::id cell) const;
    void place(model::id cell);
    // the cell being placed in the schematic, no_id if none
    model::id placing() const { return show_symbol || session || !schematic ? model::no_id : schematic->placing(); }

    bool show_symbol = false;

private:
    void toolbar();
    void edit_toolbar();
    void place_popup();
    void open_place_popup();
    bool busy() const { return (schematic && schematic->busy()) || symbol.busy(); }
    void after_undo();

    context ctx;
    model::history hist;
    // null for cells without a schematic, they have the model page
    std::unique_ptr<schematic_editor> schematic;
    std::unique_ptr<model_editor> model_page;
    symbol_editor symbol;

    bool focus_search = false;
    std::string search;

    std::shared_ptr<sim_session> session;
    std::unique_ptr<sim_view> view;

    std::string locked_by;
};

}
