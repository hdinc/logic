#pragma once

#include "schematic_editor.h"
#include "symbol_editor.h"

#include <Hd/FrameBuffer.h>
#include <Hd/Texture.h>

namespace logic::editor {

// schematic and symbol view of one cell, switched with tab
class cell_editor {
public:
    explicit cell_editor(context ctx);

    model::cell& cell() { return ctx.cell; }
    model::library& library() { return ctx.lib; }

    // draws the editor into the remaining space of the current imgui window.
    // tex/fb are shared by all editors, only the visible one is drawn.
    void gui(Hd::Texture& tex, Hd::FrameBuffer& fb);

    void property_panel();

    bool show_symbol = false;

private:
    void place_popup();

    context ctx;
    schematic_editor schematic;
    symbol_editor symbol;

    bool focus_search = false;
    std::string search;
};

}
