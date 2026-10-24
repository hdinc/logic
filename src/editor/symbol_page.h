#pragma once

#include "symbol_editor.h"
#include "../model/history.h"

#include <Hd/FrameBuffer.h>
#include <Hd/Texture.h>

#include <memory>
#include <string>

namespace logic::editor {

// the symbol of one cell in the symbol editor window: its toolbar and
// canvas. shares the undo history with the cell's editor. a cell with an
// automatic symbol shows the generated box until a symbol is added.
class symbol_page {
public:
    symbol_page(context ctx, std::shared_ptr<model::history> hist);

    model::cell& cell() { return ctx.cell; }

    // draws into the remaining space of the current imgui window. tex/fb
    // are shared by all symbol pages, only the visible one is drawn.
    void gui(Hd::Texture& tex, Hd::FrameBuffer& fb);
    void property_panel();

    // can't be edited while a running simulation uses the cell. empty if
    // not locked, otherwise the name of the simulated cell.
    void set_locked_by(std::string top) { locked_by = std::move(top); }
    bool locked() const { return !locked_by.empty(); }

    // makes the automatic symbol the cell's own, to be edited
    void add_symbol();

    // as in cell_editor, for the symbol
    bool can_undo() const;
    bool can_redo() const;
    void undo();
    void redo();
    bool can_copy() const;
    bool can_paste() const;
    void copy();
    void cut();
    void paste();

    // after the cell was changed from outside, e.g. by an undo in its editor
    void cell_replaced();

private:
    void toolbar();
    bool busy() const { return symbol.busy(); }

    context ctx;
    std::shared_ptr<model::history> hist;
    size_t seen_restores = 0;
    symbol_editor symbol;
    std::string locked_by;
};

}
