#pragma once

#include "../model/workspace.h"
#include "../render/renderer.h"

#include <imgui.h>
#include <string>

namespace logic::editor {

// what an editor works on. the cell is edited in place, there is no copy.
struct context {
    model::workspace& ws;
    model::library& lib;
    model::cell& cell;
    render::renderer& r;

    // call after every change to the cell so the library gets saved
    void modified() { lib.dirty = true; }
};

// imgui text input for std::string, returns true when the string changed
inline bool input_string(const char* label, std::string& s, ImGuiInputTextFlags flags = 0)
{
    char buf[256];
    s.copy(buf, sizeof(buf) - 1);
    buf[std::min(s.size(), sizeof(buf) - 1)] = 0;
    if (ImGui::InputText(label, buf, sizeof(buf), flags) && s != buf) {
        s = buf;
        return true;
    }
    return false;
}

}
