#include "canvas.h"

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

namespace logic::editor {

void canvas::set_size(glm::vec2 s)
{
    size = glm::max(s, glm::vec2{1.0f, 1.0f});
    recalc();
    if (anchored) {
        anchored = false;
        center += anchor - to_world({-1.0f, -1.0f});
        recalc();
    }
}

void canvas::handle_view_input()
{
    auto& io = ImGui::GetIO();

    if (io.MouseWheel != 0) {
        // keep the point under the cursor fixed while zooming
        auto p1 = mouse_world();
        zoom *= 1 + io.MouseWheel / 10.0f;
        zoom = glm::clamp(zoom, 1.0f / 10000.0f, 1.0f);
        recalc();
        center += p1 - mouse_world();
        recalc();
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        panning = true;
        pan_last = mouse;
    }

    if (panning) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            center += to_world(pan_last) - mouse_world();
            recalc();
            pan_last = mouse;
        } else {
            panning = false;
        }
    }
}

glm::vec2 canvas::to_world(glm::vec2 ndc) const
{
    auto r = glm::inverse(m_vp) * glm::vec4(ndc, 0.0f, 1.0f);
    return {r.x, r.y};
}

float canvas::pixels(float px) const
{
    return px * 2.0f / size.x / m_vp[0][0];
}

void canvas::recalc()
{
    // zoom is the scale of the shorter side, so a world unit is square
    glm::vec3 s(zoom, zoom, 1.0f);
    if (size.x > size.y)
        s.y *= size.x / size.y;
    else
        s.x *= size.y / size.x;

    m_vp = glm::translate(glm::scale(glm::mat4(1.0f), s), {-center, 0.0f});
}

}
