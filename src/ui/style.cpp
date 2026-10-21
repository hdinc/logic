#include "style.h"

#include <imgui.h>

namespace logic::ui {

void apply_style(bool light)
{
    auto& s = ImGui::GetStyle();
    if (light)
        ImGui::StyleColorsLight(&s);
    else
        ImGui::StyleColorsDark(&s);
    s.FrameRounding = 4.0f;
    s.GrabRounding = 4.0f;
    s.WindowRounding = 6.0f;
    s.PopupRounding = 4.0f;
    s.TabRounding = 4.0f;
}

}
