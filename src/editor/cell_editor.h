#pragma once

#include "schematic_editor.h"
#include "sim_view.h"
#include "symbol_editor.h"
#include "../model/history.h"

#include <Hd/FrameBuffer.h>
#include <Hd/Texture.h>

namespace logic::editor {

// schematic and symbol view of one cell, switched with tab. f5 simulates the
// cell: the editor then shows a sim_view of it and the cell can't be edited.
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

    bool show_symbol = false;

private:
    void toolbar();
    void edit_toolbar();
    void place_popup();
    void open_place_popup();
    bool busy() const { return schematic.busy() || symbol.busy(); }
    void after_undo();

    context ctx;
    model::history hist;
    schematic_editor schematic;
    symbol_editor symbol;

    bool focus_search = false;
    std::string search;

    std::shared_ptr<sim_session> session;
    std::unique_ptr<sim_view> view;

    std::string locked_by;
};

}
