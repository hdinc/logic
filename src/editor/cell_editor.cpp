#include "cell_editor.h"

#include <algorithm>
#include <cctype>

namespace logic::editor {

namespace {

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
    , schematic(c)
    , symbol(c)
{
}

void cell_editor::gui(Hd::Texture& tex, Hd::FrameBuffer& fb)
{
    using namespace ImGui;

    int view = show_symbol ? 1 : 0;
    RadioButton("Schematic", &view, 0);
    SameLine();
    RadioButton("Symbol", &view, 1);
    SameLine();
    TextDisabled("(tab to switch)");
    show_symbol = view == 1;

    auto size = GetContentRegionAvail();
    size.x = std::min(size.x, (float)tex.GetWidth());
    size.y = std::min(size.y, (float)tex.GetHeight());
    if (size.x < 1.0f || size.y < 1.0f) return;

    auto screen_pos = GetCursorScreenPos();
    auto cursor = GetCursorPos();
    InvisibleButton("canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = IsItemHovered() && IsWindowFocused();

    if (hovered && IsKeyPressed(ImGuiKey_Tab)) show_symbol = !show_symbol;

    auto mouse = GetMousePos();
    glm::vec2 ndc{2.0f * (mouse.x - screen_pos.x) / size.x - 1.0f, 1.0f - 2.0f * (mouse.y - screen_pos.y) / size.y};

    canvas& c = show_symbol ? symbol.view : schematic.view;
    c.set_size({size.x, size.y});
    c.set_mouse(ndc);

    if (show_symbol) {
        symbol.process_inputs(hovered);
    } else {
        schematic.process_inputs(hovered);
        if (hovered && schematic.is_idle() && IsKeyPressed(ImGuiKey_Space)) {
            search.clear();
            focus_search = true;
            OpenPopup("place");
        }
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
    if (show_symbol)
        symbol.property_panel();
    else
        schematic.property_panel();
}

}
