#include "cell_editor.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cctype>

namespace logic::editor {

namespace {

// toggle look for the active tool
bool tool_button(const char* label, const char* tip, bool active)
{
    using namespace ImGui;
    if (active) PushStyleColor(ImGuiCol_Button, GetStyleColorVec4(ImGuiCol_ButtonActive));
    bool clicked = Button(label);
    if (active) PopStyleColor();
    SetItemTooltip("%s", tip);
    SameLine();
    return clicked;
}

bool contains_ignore_case(const std::string& s, const std::string& sub)
{
    auto it = std::search(s.begin(), s.end(), sub.begin(), sub.end(), [](char a, char b) {
        return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
    });
    return it != s.end();
}

}

cell_editor::cell_editor(context c)
    : ctx(c)
    , hist(c.cell)
    , schematic(c)
    , symbol(c)
{
}

void cell_editor::gui(Hd::Texture& tex, Hd::FrameBuffer& fb)
{
    using namespace ImGui;

    // stopped from a view in another tab
    if (session && session->stopped) stop_simulation();

    if (IsKeyPressed(ImGuiKey_F5)) {
        if (session)
            stop_simulation();
        else
            start_simulation();
    }

    toolbar();

    if (view) {
        view->gui(tex, fb);
        return;
    }

    auto size = GetContentRegionAvail();
    size.x = std::min(size.x, (float)tex.GetWidth());
    size.y = std::min(size.y, (float)tex.GetHeight());
    if (size.x < 1.0f || size.y < 1.0f) return;

    auto screen_pos = GetCursorScreenPos();
    auto cursor = GetCursorPos();
    InvisibleButton("canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = IsItemHovered() && IsWindowFocused();

    if (hovered && !busy() && IsKeyPressed(ImGuiKey_Tab)) show_symbol = !show_symbol;

    auto mouse = GetMousePos();
    glm::vec2 ndc{2.0f * (mouse.x - screen_pos.x) / size.x - 1.0f, 1.0f - 2.0f * (mouse.y - screen_pos.y) / size.y};

    canvas& c = show_symbol ? symbol.view : schematic.view;
    c.set_size({size.x, size.y});
    c.set_mouse(ndc);

    if (locked()) {
        // look but don't touch
        if (hovered) c.handle_view_input();
    } else if (show_symbol) {
        symbol.process_inputs(hovered);
    } else {
        schematic.process_inputs(hovered);
        if (hovered && !schematic.busy() && IsKeyPressed(ImGuiKey_Space)) open_place_popup();
    }

    fb.Bind((int)size.x, (int)size.y);
    if (show_symbol)
        symbol.draw();
    else
        schematic.draw();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // the texture is bigger than the view, only show the drawn corner
    SetCursorPos(cursor);
    Image((ImTextureID)tex.Id(), size, {0, size.y / tex.GetHeight()}, {size.x / tex.GetWidth(), 0});

    place_popup();

    // one undo step per finished edit: not during a drag or while typing
    // into a property
    if (!busy() && !IsAnyItemActive()) hist.commit(ctx.cell);
}

void cell_editor::toolbar()
{
    using namespace ImGui;

    BeginDisabled(busy());
    if (tool_button(session ? "Stop" : "Simulate", session ? "Back to editing (F5)" : "Simulate the schematic (F5)", session != nullptr)) {
        if (session)
            stop_simulation();
        else
            start_simulation();
    }
    EndDisabled();

    SeparatorEx(ImGuiSeparatorFlags_Vertical);
    SameLine();

    // the sim view draws its controls on this line
    if (!session) edit_toolbar();
}

void cell_editor::edit_toolbar()
{
    using namespace ImGui;

    int view = show_symbol ? 1 : 0;
    RadioButton("Schematic", &view, 0);
    SetItemTooltip("Tab to switch");
    SameLine();
    RadioButton("Symbol", &view, 1);
    SetItemTooltip("Tab to switch");
    SameLine();
    if (!busy()) show_symbol = view == 1;

    SeparatorEx(ImGuiSeparatorFlags_Vertical);
    SameLine();

    BeginDisabled(busy() || locked());
    if (show_symbol) {
        using k = symbol_editor::tool_kind;
        auto t = symbol.current_tool();
        if (tool_button("Select", "Select and move (Esc)", t == k::select)) symbol.set_tool(k::select);
        if (tool_button("Pin", "Place a pin linked to a port (P)", t == k::pin)) symbol.set_tool(k::pin);
        if (tool_button("Line", "Draw a line (L)", t == k::line)) symbol.set_tool(k::line);
        if (tool_button("Bezier", "Draw a curve: both ends, then the control point (B)", t == k::bezier)) symbol.set_tool(k::bezier);
        if (tool_button("Circle", "Draw a circle: center, then radius (C)", t == k::circle)) symbol.set_tool(k::circle);
        if (tool_button("Text", "Place a text (T)", t == k::text)) symbol.set_tool(k::text);
    } else {
        using k = schematic_editor::tool_kind;
        auto t = schematic.current_tool();
        if (tool_button("Select", "Select and move (Esc)", t == k::select)) schematic.set_tool(k::select);
        if (tool_button("Wire", "Draw wires, Esc ends a wire (W)", t == k::wire)) schematic.set_tool(k::wire);
        if (tool_button("Input", "Place an input port (I)", t == k::input_port)) schematic.set_tool(k::input_port);
        if (tool_button("Output", "Place an output port (O)", t == k::output_port)) schematic.set_tool(k::output_port);
        if (tool_button("Cell...", "Place an instance of a cell (Space)", t == k::place_instance)) open_place_popup();
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

    SameLine();
    if (locked())
        TextColored({1, 0.6f, 0.2f, 1}, "used by the simulation of '%s', stop it to edit", locked_by.c_str());
    else
        TextDisabled("Shift: keep placing");
}

void cell_editor::start_simulation()
{
    if (session || busy()) return;

    hist.commit(ctx.cell);
    schematic.set_tool(schematic_editor::tool_kind::select);
    schematic.selection.clear();
    show_symbol = false;

    session = std::make_shared<sim_session>(ctx.ws, ctx.cell);
    view = std::make_unique<sim_view>(ctx.ws, ctx.r, session);
}

void cell_editor::stop_simulation()
{
    // views of it in other tabs close
    if (session) session->stopped = true;
    view.reset();
    session.reset();
}

void cell_editor::open_place_popup()
{
    search.clear();
    focus_search = true;
    ImGui::OpenPopup("place");
}

bool cell_editor::can_undo() const
{
    return !busy() && !session && !locked() && hist.can_undo();
}

bool cell_editor::can_redo() const
{
    return !busy() && !session && !locked() && hist.can_redo();
}

void cell_editor::undo()
{
    if (can_undo() && hist.undo(ctx.cell)) after_undo();
}

void cell_editor::redo()
{
    if (can_redo() && hist.redo(ctx.cell)) after_undo();
}

void cell_editor::after_undo()
{
    ctx.modified();
    schematic.cell_replaced();
    symbol.cell_replaced();
}

void cell_editor::place_popup()
{
    using namespace ImGui;

    if (!BeginPopup("place")) return;

    if (focus_search) {
        SetKeyboardFocusHere();
        focus_search = false;
    }
    input_string("Search", search);

    BeginChild("results", {300, 250});
    for (auto& lib : ctx.ws.libraries) {
        for (auto& c : lib->cells) {
            if (!search.empty() && !contains_ignore_case(c->name, search)) continue;

            // a cell can't contain itself, directly or through other cells
            bool cycle = ctx.ws.would_create_cycle(ctx.cell.id, c->id);
            auto label = lib->name + " / " + c->name;

            BeginDisabled(cycle);
            if (Selectable(label.c_str())) {
                schematic.begin_place(c->id);
                CloseCurrentPopup();
            }
            EndDisabled();
            if (cycle) SetItemTooltip("would contain itself");
        }
    }
    EndChild();

    EndPopup();
}

void cell_editor::property_panel()
{
    if (view) {
        view->property_panel();
        return;
    }

    if (locked()) ImGui::TextColored({1, 0.6f, 0.2f, 1}, "Used by the simulation of '%s'", locked_by.c_str());
    ImGui::BeginDisabled(locked());
    if (show_symbol)
        symbol.property_panel();
    else
        schematic.property_panel();
    ImGui::EndDisabled();
}

}
