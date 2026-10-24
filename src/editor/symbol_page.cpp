#include "symbol_page.h"
#include "canvas_widget.h"
#include "../io/json.h"
#include "../ui/theme.h"

#include <imgui_internal.h>

namespace logic::editor {

symbol_page::symbol_page(context c, std::shared_ptr<model::history> h)
    : ctx(c)
    , hist(std::move(h))
    , symbol(c)
{
}

void symbol_page::gui(Hd::Texture& tex, Hd::FrameBuffer& fb)
{
    using namespace ImGui;

    // undone or redone in the cell's editor
    if (hist->restore_count() != seen_restores) {
        seen_restores = hist->restore_count();
        symbol.cell_replaced();
    }

    toolbar();

    // the automatic box and a locked cell can only be looked at
    bool view_only = locked() || ctx.cell.auto_symbol();
    auto input = [&](bool hovered) {
        if (!view_only)
            symbol.process_inputs(hovered);
        else if (hovered)
            symbol.view.handle_view_input();
    };
    canvas_widget(symbol.view, tex, fb, input, [&] { symbol.draw(); });

    // one undo step per finished edit: not during a drag or while typing
    // into a property
    if (!busy() && !IsAnyItemActive()) hist->commit(ctx.cell);
}

void symbol_page::toolbar()
{
    using namespace ImGui;

    BeginDisabled(locked());
    if (ctx.cell.auto_symbol()) {
        if (tool_button("Add symbol", "Make the box the cell's own symbol, to draw on it", false)) add_symbol();
        TextDisabled("Automatic: a box that follows the ports");
        SameLine();
    } else {
        using k = symbol_editor::tool_kind;
        auto t = symbol.current_tool();
        if (tool_button("Select", "Select and move (Esc)", t == k::select)) symbol.set_tool(k::select);
        if (tool_button("Pin", "Place a pin linked to a port (P)", t == k::pin)) symbol.set_tool(k::pin);
        if (tool_button("Line", "Draw a line (L)", t == k::line)) symbol.set_tool(k::line);
        if (tool_button("Bezier", "Draw a curve: both ends, then the control point (B)", t == k::bezier)) symbol.set_tool(k::bezier);
        if (tool_button("Arc", "Draw an arc: both ends, then a point on it (A)", t == k::arc)) symbol.set_tool(k::arc);
        if (tool_button("Circle", "Draw a circle: center, then radius (C)", t == k::circle)) symbol.set_tool(k::circle);
        if (tool_button("Text", "Place a text (T)", t == k::text)) symbol.set_tool(k::text);
        if (tool_button("Rect", "Draw a rectangle: two corners (R)", t == k::rect)) symbol.set_tool(k::rect);
    }
    EndDisabled();

    SeparatorEx(ImGuiSeparatorFlags_Vertical);
    SameLine();

    BeginDisabled(!can_undo());
    if (Button("Undo")) undo();
    EndDisabled();
    SetItemTooltip("Ctrl+Z");
    SameLine();
    BeginDisabled(!can_redo());
    if (Button("Redo")) redo();
    EndDisabled();
    SetItemTooltip("Ctrl+Y");

    if (locked()) {
        SameLine();
        TextColored(ui::color(ui::color_id::warning), "used by the simulation of '%s', stop it to edit", locked_by.c_str());
    }
}

void symbol_page::property_panel()
{
    if (locked()) ImGui::TextColored(ui::color(ui::color_id::warning), "Used by the simulation of '%s'", locked_by.c_str());
    ImGui::BeginDisabled(locked());
    symbol.property_panel();
    ImGui::EndDisabled();
}

void symbol_page::add_symbol()
{
    if (locked() || busy() || !ctx.cell.auto_symbol()) return;
    ctx.cell.generate_symbol(model::pin_order::position);
    ctx.modified();
}

bool symbol_page::can_undo() const
{
    return !locked() && hist->can_undo();
}

bool symbol_page::can_redo() const
{
    return !locked() && hist->can_redo();
}

void symbol_page::undo()
{
    if (!busy() && can_undo() && hist->undo(ctx.cell)) cell_replaced();
}

void symbol_page::redo()
{
    if (!busy() && can_redo() && hist->redo(ctx.cell)) cell_replaced();
}

bool symbol_page::can_copy() const
{
    return !busy() && symbol.copy_selection().has_value();
}

bool symbol_page::can_paste() const
{
    return !busy() && !locked() && !ctx.cell.auto_symbol() && ImGui::GetClipboardText() != nullptr;
}

void symbol_page::copy()
{
    if (!can_copy()) return;
    ImGui::SetClipboardText(io::to_clipboard(*symbol.copy_selection(), "symbol").c_str());
}

void symbol_page::cut()
{
    if (!can_copy() || locked()) return;
    copy();
    symbol.delete_selection();
}

void symbol_page::paste()
{
    if (!can_paste()) return;
    auto part = io::from_clipboard(ImGui::GetClipboardText(), "symbol");
    if (part) symbol.begin_paste(std::move(*part));
}

void symbol_page::cell_replaced()
{
    ctx.modified();
    symbol.cell_replaced();
}

}
