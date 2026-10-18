#include "cell_editor.h"
#include "../builtin/builtin.h"

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

    toolbar();

    auto size = GetContentRegionAvail();
    size.x = std::min(size.x, (float)tex.GetWidth());
    size.y = std::min(size.y, (float)tex.GetHeight());
    if (size.x < 1.0f || size.y < 1.0f) return;

    auto screen_pos = GetCursorScreenPos();
    auto cursor = GetCursorPos();
    InvisibleButton("canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = IsItemHovered() && IsWindowFocused();

    if (IsKeyPressed(ImGuiKey_F5)) {
        if (simulation)
            stop_simulation();
        else
            start_simulation();
    }
    if (hovered && !busy() && !simulation && IsKeyPressed(ImGuiKey_Tab)) show_symbol = !show_symbol;

    auto mouse = GetMousePos();
    glm::vec2 ndc{2.0f * (mouse.x - screen_pos.x) / size.x - 1.0f, 1.0f - 2.0f * (mouse.y - screen_pos.y) / size.y};

    canvas& c = show_symbol ? symbol.view : schematic.view;
    c.set_size({size.x, size.y});
    c.set_mouse(ndc);

    if (simulation) {
        if (hovered) schematic.view.handle_view_input();
        sim_inputs(hovered);
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
    if (!simulation && !busy() && !IsAnyItemActive()) hist.commit(ctx.cell);
}

void cell_editor::toolbar()
{
    using namespace ImGui;

    BeginDisabled(busy());
    if (tool_button(simulation ? "Stop" : "Simulate", simulation ? "Back to editing (F5)" : "Simulate the schematic (F5)", simulation != nullptr)) {
        if (simulation)
            stop_simulation();
        else
            start_simulation();
    }
    EndDisabled();

    SeparatorEx(ImGuiSeparatorFlags_Vertical);
    SameLine();

    if (simulation)
        sim_toolbar();
    else
        edit_toolbar();
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

    BeginDisabled(busy());
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
    TextDisabled("Shift: keep placing");
}

void cell_editor::sim_toolbar()
{
    using namespace ImGui;

    if (Button("Reset")) {
        simulation->reset();
        running = false;
    }
    SetItemTooltip("Back to the initial state, inputs keep their values");
    SameLine();

    if (Button("Step")) simulation->tick();
    SetItemTooltip("One clock tick (Space)");
    SameLine();

    if (tool_button(running ? "Pause" : "Run", "Tick the clocks continuously", running)) {
        running = !running;
        tick_accum = 0.0;
    }

    SetNextItemWidth(160);
    DragFloat("##tps", &ticks_per_second, 0.5f, 0.5f, 1000.0f, "%.1f ticks/s", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
    SameLine();

    Text("tick %llu", (unsigned long long)simulation->ticks());
    if (simulation->oscillating()) {
        SameLine();
        TextColored({1, 0.3f, 0.3f, 1}, "oscillating");
    }
    if (!simulation->problems().empty()) {
        SameLine();
        TextColored({1, 0.6f, 0.2f, 1}, "%zu problem(s), see properties", simulation->problems().size());
    }
}

void cell_editor::sim_inputs(bool hovered)
{
    using namespace ImGui;

    if (hovered && IsKeyPressed(ImGuiKey_Space)) simulation->tick();

    // click on a single bit input port toggles it
    if (hovered && IsMouseClicked(ImGuiMouseButton_Left)) {
        for (auto& h : schematic.hit_test(schematic.view.mouse_world())) {
            if (h.kind != schematic_editor::handle::port) continue;
            for (int k = 0; k < (int)ctx.cell.ports.size(); k++) {
                auto& p = ctx.cell.ports[k];
                if (p.id != h.id || p.dir != model::direction::input || p.width != 1) continue;
                auto v = simulation->input_value(k);
                simulation->set_input(k, sim::known(!(v.bits & 1), 1));
            }
        }
    }

    if (running) {
        tick_accum += GetIO().DeltaTime * ticks_per_second;
        // don't fall behind forever when ticks are slow
        int n = (int)std::min(tick_accum, 1000.0);
        tick_accum = n == 1000 ? 0.0 : tick_accum - n;
        for (int i = 0; i < n && running; i++) {
            simulation->tick();
            if (simulation->oscillating()) running = false;
        }
    }
}

void cell_editor::start_simulation()
{
    if (simulation || busy()) return;

    hist.commit(ctx.cell);
    schematic.set_tool(schematic_editor::tool_kind::select);
    schematic.selection.clear();
    show_symbol = false;

    simulation = std::make_unique<sim::simulator>(ctx.ws, ctx.cell, builtin::factory());
    schematic.sim = simulation.get();
    running = false;
}

void cell_editor::stop_simulation()
{
    schematic.sim = nullptr;
    simulation.reset();
    running = false;
}

void cell_editor::sim_panel()
{
    using namespace ImGui;

    auto& s = *simulation;

    SeparatorText("Simulation");
    Text("Tick %llu", (unsigned long long)s.ticks());
    if (s.oscillating()) TextColored({1, 0.3f, 0.3f, 1}, "The circuit doesn't settle, e.g. a gate feeding itself");
    for (auto& p : s.problems())
        TextColored({1, 0.6f, 0.2f, 1}, "%s", p.c_str());

    auto& ports = ctx.cell.ports;

    SeparatorText("Inputs");
    for (int k = 0; k < (int)ports.size(); k++) {
        auto& p = ports[k];
        if (p.dir == model::direction::output) continue;
        PushID(k);
        auto v = s.input_value(k);
        if (p.width == 1) {
            bool b = v.bits & 1;
            if (Checkbox(p.name.c_str(), &b)) s.set_input(k, sim::known(b, 1));
        } else {
            uint64_t u = v.bits;
            if (InputScalar(p.name.c_str(), ImGuiDataType_U64, &u, nullptr, nullptr, "%llX", ImGuiInputTextFlags_CharsHexadecimal))
                s.set_input(k, sim::known(u, p.width));
            SetItemTooltip("hex, %d bits", p.width);
        }
        PopID();
    }

    SeparatorText("Outputs");
    if (BeginTable("outputs", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        for (int k = 0; k < (int)ports.size(); k++) {
            auto& p = ports[k];
            if (p.dir == model::direction::input) continue;
            TableNextRow();
            TableNextColumn();
            TextUnformatted(p.name.c_str());
            TableNextColumn();
            auto v = s.port_value(k);
            auto c = render::value_color(v, p.width);
            TextColored({c.r, c.g, c.b, c.a}, "%s", sim::to_string(v, p.width).c_str());
        }
        EndTable();
    }
}

void cell_editor::open_place_popup()
{
    search.clear();
    focus_search = true;
    ImGui::OpenPopup("place");
}

bool cell_editor::can_undo() const
{
    return !busy() && !simulation && hist.can_undo();
}

bool cell_editor::can_redo() const
{
    return !busy() && !simulation && hist.can_redo();
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
    if (simulation)
        sim_panel();
    else if (show_symbol)
        symbol.property_panel();
    else
        schematic.property_panel();
}

}
