#pragma once

#include "../hdl/manager.h"
#include "../model/workspace.h"
#include "../plugin/registry.h"
#include "../render/renderer.h"
#include "../ui/search.h"

#include <imgui.h>
#include <string>

namespace logic::editor {

// what an editor works on. the cell is edited in place, there is no copy.
struct context {
    model::workspace& ws;
    model::library& lib;
    model::cell& cell;
    render::renderer& r;
    // creates the simulation models, the builtins only if null
    const sim::component_factory* factory = nullptr;
    // c model plugins, null if there are none
    const plugin::registry* plugins = nullptr;
    // hdl cells, null if they aren't supported
    hdl::manager* hdl = nullptr;
    // cells placed lately, shared by the editors. null if not kept.
    ui::recent_list* recent = nullptr;

    // call after every change to the cell so the library gets saved
    void modified()
    {
        lib.dirty = true;
        if (!lib.symbols_dir.empty()) lib.changed_symbols.insert(cell.id);
    }
};

// a toolbar button with the toggled look for the active tool, followed by
// SameLine
inline bool tool_button(const char* label, const char* tip, bool active)
{
    using namespace ImGui;
    if (active) PushStyleColor(ImGuiCol_Button, GetStyleColorVec4(ImGuiCol_ButtonActive));
    bool clicked = Button(label);
    if (active) PopStyleColor();
    SetItemTooltip("%s", tip);
    SameLine();
    return clicked;
}

// imgui text input for std::string, returns true when the string changed.
// the hint is shown faintly while it's empty.
inline bool input_string(const char* label, std::string& s, ImGuiInputTextFlags flags = 0, const char* hint = nullptr)
{
    char buf[256];
    s.copy(buf, sizeof(buf) - 1);
    buf[std::min(s.size(), sizeof(buf) - 1)] = 0;
    bool edited = hint ? ImGui::InputTextWithHint(label, hint, buf, sizeof(buf), flags) : ImGui::InputText(label, buf, sizeof(buf), flags);
    if (edited && s != buf) {
        s = buf;
        return true;
    }
    return false;
}

// multi line text input for std::string
inline bool input_multiline(const char* label, std::string& s, ImVec2 size = {0, 0})
{
    char buf[2048];
    s.copy(buf, sizeof(buf) - 1);
    buf[std::min(s.size(), sizeof(buf) - 1)] = 0;
    if (ImGui::InputTextMultiline(label, buf, sizeof(buf), size) && s != buf) {
        s = buf;
        return true;
    }
    return false;
}

}
