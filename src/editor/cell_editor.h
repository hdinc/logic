#pragma once

#include "schematic_editor.h"
#include "symbol_editor.h"
#include "../model/history.h"

#include <Hd/FrameBuffer.h>
#include <Hd/Texture.h>

namespace logic::editor {

// schematic and symbol view of one cell, switched with tab. f5 switches to
// simulate mode: the schematic can't be edited then, it shows the values and
// the inputs are clicked to toggle them.
class cell_editor {
public:
    explicit cell_editor(context ctx);

    model::cell& cell() { return ctx.cell; }
    model::library& library() { return ctx.lib; }

    // draws the editor into the remaining space of the current imgui window.
    // tex/fb are shared by all editors, only the visible one is drawn.
    void gui(Hd::Texture& tex, Hd::FrameBuffer& fb);

    void property_panel();

    bool simulating() const { return simulation != nullptr; }
    // ignored while a tool is busy
    void start_simulation();
    void stop_simulation();

    // ignored while a tool is busy (e.g. during a drag) or simulating
    bool can_undo() const;
    bool can_redo() const;
    void undo();
    void redo();

    bool show_symbol = false;

private:
    void toolbar();
    void edit_toolbar();
    void sim_toolbar();
    void sim_inputs(bool hovered);
    void sim_panel();
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

    std::unique_ptr<sim::simulator> simulation;
    bool running = false;
    float ticks_per_second = 10.0f;
    double tick_accum = 0.0;
};

}
