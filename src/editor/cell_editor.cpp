#include "cell_editor.h"
#include "../builtin/builtin.h"
#include "../io/json.h"
#include "../ui/theme.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

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

}

cell_editor::cell_editor(context c)
    : ctx(c)
    , hist(c.cell)
    , symbol(c)
{
    if (c.cell.get_schematic())
        schematic = std::make_unique<schematic_editor>(c);
    else
        model_page = std::make_unique<model_editor>(c);
}

void cell_editor::gui(Hd::Texture& tex, Hd::FrameBuffer& fb)
{
    using namespace ImGui;

    canvas_rect.reset();

    // stopped from a view in another tab
    if (session && session->stopped) stop_simulation();

    // the builds for the simulation are done
    if (!building.empty() && !ctx.hdl->busy()) {
        for (auto id : building) {
            auto st = ctx.hdl->status(id);
            auto c = ctx.ws.find_cell(id);
            if (st.state == hdl::build_status::failed) start_error = "building '" + (c ? c->name : "?") + "' failed: " + st.error;
        }
        building.clear();
        if (start_error.empty()) start_simulation();
    }

    if (IsKeyPressed(ImGuiKey_F5) && schematic) {
        if (session)
            stop_simulation();
        else if (!building.empty())
            building.clear();
        else
            start_simulation();
    }

    toolbar();

    if (view) {
        view->gui(tex, fb);
        return;
    }

    if (!schematic && !show_symbol) {
        model_page->gui(locked());
        if (!IsAnyItemActive()) hist.commit(ctx.cell);
        return;
    }

    // whole pixels, the framebuffer is shown 1:1 so small text stays sharp
    auto size = GetContentRegionAvail();
    size.x = std::min(std::floor(size.x), (float)tex.GetWidth());
    size.y = std::min(std::floor(size.y), (float)tex.GetHeight());
    if (size.x < 1.0f || size.y < 1.0f) return;

    auto screen_pos = GetCursorScreenPos();
    auto cursor = GetCursorPos();
    InvisibleButton("canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = IsItemHovered() && IsWindowFocused();

    if (hovered && !busy() && IsKeyPressed(ImGuiKey_Tab)) show_symbol = !show_symbol;

    auto mouse = GetMousePos();
    glm::vec2 ndc{2.0f * (mouse.x - screen_pos.x) / size.x - 1.0f, 1.0f - 2.0f * (mouse.y - screen_pos.y) / size.y};

    canvas& c = show_symbol ? symbol.view : schematic->view;
    c.begin_frame({size.x, size.y});
    c.set_mouse(ndc);

    if (locked()) {
        // look but don't touch
        if (hovered) c.handle_view_input();
    } else if (show_symbol) {
        symbol.process_inputs(hovered);
    } else {
        schematic->process_inputs(hovered);
        if (hovered && !schematic->busy() && IsKeyPressed(ImGuiKey_Space)) open_place_popup();
    }

    fb.Bind((int)size.x, (int)size.y);
    if (show_symbol)
        symbol.draw();
    else
        schematic->draw();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // the texture is bigger than the view, only show the drawn corner
    SetCursorPos(cursor);
    Image((ImTextureID)tex.Id(), size, {0, size.y / tex.GetHeight()}, {size.x / tex.GetWidth(), 0});
    if (!show_symbol && !locked()) canvas_rect = screen_rect{screen_pos, {screen_pos.x + size.x, screen_pos.y + size.y}};

    place_popup();

    // one undo step per finished edit: not during a drag or while typing
    // into a property
    if (!busy() && !IsAnyItemActive()) hist.commit(ctx.cell);
}

void cell_editor::toolbar()
{
    using namespace ImGui;

    // not greyed out while dragging: the buttons can't be clicked then
    // anyway, and what they do checks busy() itself
    BeginDisabled(!schematic);
    if (!building.empty()) {
        if (tool_button("Cancel", "Don't wait for the builds, they go on in the background (F5)", true)) building.clear();
        TextColored(ui::color(ui::color_id::info), "building %zu HDL model(s)...", building.size());
        SameLine();
    } else if (tool_button(session ? "Stop" : "Simulate", session ? "Back to editing (F5)" : "Simulate the schematic (F5)", session != nullptr)) {
        if (session)
            stop_simulation();
        else
            start_simulation();
    }
    EndDisabled();
    if (!start_error.empty() && !session) {
        TextColored(ui::color(ui::color_id::error), "%s", start_error.c_str());
        SameLine();
        if (SmallButton("x")) start_error.clear();
        SameLine();
    }

    SeparatorEx(ImGuiSeparatorFlags_Vertical);
    SameLine();

    // the sim view draws its controls on this line
    if (!session) edit_toolbar();
}

void cell_editor::edit_toolbar()
{
    using namespace ImGui;

    int view = show_symbol ? 1 : 0;
    RadioButton(schematic ? "Schematic" : "Model", &view, 0);
    SetItemTooltip("Tab to switch");
    SameLine();
    RadioButton("Symbol", &view, 1);
    SetItemTooltip("Tab to switch");
    SameLine();
    if (!busy()) show_symbol = view == 1;

    SeparatorEx(ImGuiSeparatorFlags_Vertical);
    SameLine();

    BeginDisabled(locked());
    if (!schematic && !show_symbol) {
        // the model page has its own buttons
    } else if (show_symbol) {
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
    } else {
        using k = schematic_editor::tool_kind;
        auto t = schematic->current_tool();
        if (tool_button("Select", "Select and move (Esc)", t == k::select)) schematic->set_tool(k::select);
        if (tool_button("Wire", "Draw wires, Esc ends the wire and the tool (W)", t == k::wire)) schematic->set_tool(k::wire);
        if (tool_button("Input", "Place an input port (I)", t == k::input_port)) schematic->set_tool(k::input_port);
        if (tool_button("Output", "Place an output port (O)", t == k::output_port)) schematic->set_tool(k::output_port);
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
        TextColored(ui::color(ui::color_id::warning), "used by the simulation of '%s', stop it to edit", locked_by.c_str());
    else
        TextDisabled("Shift: keep placing");
}

void cell_editor::start_simulation()
{
    if (session || busy() || !schematic || !building.empty()) return;
    start_error.clear();

    // hdl models are built first, in the background
    if (ctx.hdl) {
        for (auto& r : ctx.hdl->missing_builds(ctx.cell)) {
            if (auto c = ctx.ws.find_cell(r.cell)) {
                ctx.hdl->build(*c, r.values);
                if (std::find(building.begin(), building.end(), r.cell) == building.end()) building.push_back(r.cell);
            }
        }
        if (!building.empty()) return;
    }

    hist.commit(ctx.cell);
    schematic->set_tool(schematic_editor::tool_kind::select);
    schematic->selection.clear();
    show_symbol = false;

    session = std::make_shared<sim_session>(ctx.ws, ctx.cell, ctx.factory ? *ctx.factory : builtin::factory());
    view = std::make_unique<sim_view>(ctx.ws, ctx.r, session);
    // the camera stays where it is
    view->camera_of(ctx.cell).copy_camera(schematic->view);
}

void cell_editor::stop_simulation()
{
    // views of it in other tabs close
    if (session) session->stopped = true;
    // back to editing where the top cell was looked at
    if (view && schematic) schematic->view.copy_camera(view->camera_of(ctx.cell));
    view.reset();
    session.reset();
}

void cell_editor::open_place_popup()
{
    search.clear();
    focus_search = true;
    reset_selection = true;
    ImGui::OpenPopup("place");
}

bool cell_editor::can_undo() const
{
    return !session && !locked() && hist.can_undo();
}

bool cell_editor::can_redo() const
{
    return !session && !locked() && hist.can_redo();
}

void cell_editor::undo()
{
    if (!busy() && can_undo() && hist.undo(ctx.cell)) after_undo();
}

void cell_editor::redo()
{
    if (!busy() && can_redo() && hist.redo(ctx.cell)) after_undo();
}

bool cell_editor::can_copy() const
{
    if (!show_symbol && !schematic) return false;
    return !busy() && !session && (show_symbol ? symbol.copy_selection() : schematic->copy_selection()).has_value();
}

bool cell_editor::can_paste() const
{
    if (!show_symbol && !schematic) return false;
    return !busy() && !session && !locked() && ImGui::GetClipboardText() != nullptr;
}

void cell_editor::copy()
{
    if (!can_copy()) return;
    auto part = show_symbol ? symbol.copy_selection() : schematic->copy_selection();
    ImGui::SetClipboardText(io::to_clipboard(*part, show_symbol ? "symbol" : "schematic").c_str());
}

void cell_editor::cut()
{
    if (!can_copy() || locked()) return;
    copy();
    if (show_symbol)
        symbol.delete_selection();
    else
        schematic->delete_selection();
}

void cell_editor::paste()
{
    if (!can_paste()) return;
    auto part = io::from_clipboard(ImGui::GetClipboardText(), show_symbol ? "symbol" : "schematic");
    if (!part) return;
    if (show_symbol)
        symbol.begin_paste(std::move(*part));
    else
        schematic->begin_paste(std::move(*part));
}

bool cell_editor::can_place(model::id cell) const
{
    return schematic && !session && !locked() && !ctx.ws.would_create_cycle(ctx.cell.id, cell);
}

void cell_editor::place(model::id cell)
{
    if (busy() || !can_place(cell)) return;
    show_symbol = false;
    schematic->begin_place(cell);
}

void cell_editor::after_undo()
{
    ctx.modified();
    if (schematic) schematic->cell_replaced();
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
    SetNextItemWidth(-FLT_MIN);
    input_string("##search", search, 0, "Search cells...");

    // without text the recently placed cells, then the others. with text
    // the cells whose name contains it, the best matches first.
    struct entry {
        const model::library* lib;
        const model::cell* cell;
        ui::text_match match;
        // a cell can't contain itself, directly or through other cells
        bool cycle = false;
        // a line before it, after the recent cells
        bool separator = false;
    };
    std::vector<entry> entries;
    auto add = [&](const model::library& lib, const model::cell& c, ui::text_match m) {
        entries.push_back({&lib, &c, m, ctx.ws.would_create_cycle(ctx.cell.id, c.id)});
    };
    if (search.empty()) {
        std::vector<model::id> recent;
        if (ctx.recent) recent = ctx.recent->items();
        // in the order of the list, gone cells are left out
        for (auto id : recent)
            for (auto& lib : ctx.ws.libraries)
                if (auto c = lib->find_cell(id)) add(*lib, *c, {});
        size_t n = entries.size();
        for (auto& lib : ctx.ws.libraries)
            for (auto& c : lib->cells)
                if (std::find(recent.begin(), recent.end(), c->id) == recent.end()) add(*lib, *c, {});
        if (n > 0 && n < entries.size()) entries[n].separator = true;
    } else {
        for (auto& lib : ctx.ws.libraries)
            for (auto& c : lib->cells)
                if (auto m = ui::match_text(c->name, search)) add(*lib, *c, *m);
        std::stable_sort(entries.begin(), entries.end(), [](auto& a, auto& b) { return a.match.rank < b.match.rank; });
    }

    // the selection: the first entry that can be placed after every change
    // of the text, up and down move it, enter places it
    auto selectable = [&](int k) { return k >= 0 && k < (int)entries.size() && !entries[k].cycle; };
    if (reset_selection || search != last_search || !selectable(selected)) {
        selected = -1;
        for (int k = 0; k < (int)entries.size() && selected < 0; k++)
            if (selectable(k)) selected = k;
        last_search = search;
        reset_selection = false;
        scroll_to_selected = true;
    }
    auto step = [&](int d) {
        for (int k = selected + d; k >= 0 && k < (int)entries.size(); k += d) {
            if (selectable(k)) {
                selected = k;
                scroll_to_selected = true;
                return;
            }
        }
    };
    if (IsKeyPressed(ImGuiKey_UpArrow)) step(-1);
    if (IsKeyPressed(ImGuiKey_DownArrow)) step(1);

    auto choose = [&](int k) {
        schematic->begin_place(entries[k].cell->id);
        CloseCurrentPopup();
    };
    bool enter = IsKeyPressed(ImGuiKey_Enter, false) || IsKeyPressed(ImGuiKey_KeypadEnter, false);
    if (enter && selectable(selected)) choose(selected);
    if (IsKeyPressed(ImGuiKey_Escape, false)) CloseCurrentPopup();

    BeginChild("results", {300, 250});
    auto dl = GetWindowDrawList();
    for (int k = 0; k < (int)entries.size(); k++) {
        auto& e = entries[k];
        if (e.separator) Separator();

        PushID(k);
        BeginDisabled(e.cycle);
        // the row's rect starts left of the content, the text at the cursor
        auto pos = GetCursorScreenPos();
        bool clicked = Selectable("##cell", k == selected);
        // "library / name", the library faint and the found text marked
        auto text = [&](const std::string& s, ImU32 color) {
            dl->AddText(pos, color, s.c_str());
            pos.x += CalcTextSize(s.c_str()).x;
        };
        auto& name = e.cell->name;
        size_t len = search.empty() ? 0 : search.size();
        text(e.lib->name + " / ", GetColorU32(ImGuiCol_TextDisabled));
        text(name.substr(0, e.match.pos), GetColorU32(ImGuiCol_Text));
        text(name.substr(e.match.pos, len), GetColorU32(ImGuiCol_PlotLinesHovered));
        text(name.substr(e.match.pos + len), GetColorU32(ImGuiCol_Text));
        EndDisabled();
        if (e.cycle) SetItemTooltip("would contain itself");
        PopID();

        if (clicked) choose(k);
        if (k == selected && scroll_to_selected) {
            SetScrollHereY();
            scroll_to_selected = false;
        }
    }
    if (entries.empty()) TextDisabled("no cell matches");
    EndChild();

    EndPopup();
}

void cell_editor::property_panel()
{
    if (view) {
        view->property_panel();
        return;
    }

    if (locked()) ImGui::TextColored(ui::color(ui::color_id::warning), "Used by the simulation of '%s'", locked_by.c_str());
    ImGui::BeginDisabled(locked());
    if (show_symbol)
        symbol.property_panel();
    else if (schematic)
        schematic->property_panel();
    else
        ImGui::TextDisabled("The model is set up in the editor");
    ImGui::EndDisabled();
}

}
