#include "canvas.h"

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <cmath>

namespace logic::editor {

void canvas::begin_frame(glm::vec2 s)
{
    size = glm::max(s, glm::vec2{1.0f, 1.0f});
    recalc();

    if (target_zoom != zoom) {
        // most of the way in about 0.15 s, evenly in the logarithm
        float t = 1.0f - std::exp(-ImGui::GetIO().DeltaTime * 20.0f);
        float k = std::log(target_zoom / zoom);
        zoom = std::abs(k) < 0.002f ? target_zoom : zoom * std::exp(k * t);
        recalc();
        center += zoom_world - to_world(zoom_ndc);
        recalc();
    }
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
        // the point under the cursor stays there while zooming
        target_zoom = glm::clamp(target_zoom * std::pow(1.2f, io.MouseWheel), 1.0f / 10000.0f, 1.0f);
        zoom_ndc = mouse;
        zoom_world = mouse_world();
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
            // a zoom going on keeps the point under the cursor
            zoom_world = to_world(zoom_ndc);
        } else {
            panning = false;
        }
    }

    // middle drag: up zooms in, down out, about where the drag started
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        drag_zooming = true;
        zoom_last_y = mouse.y;
        zoom_ndc = mouse;
        zoom_world = mouse_world();
    }
    if (drag_zooming) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            float dy = mouse.y - zoom_last_y;
            zoom_last_y = mouse.y;
            if (dy != 0.0f) {
                // the height of the view is a factor of about 20
                zoom = target_zoom = glm::clamp(zoom * std::exp(dy * 1.5f), 1.0f / 10000.0f, 1.0f);
                recalc();
                center += zoom_world - to_world(zoom_ndc);
                recalc();
            }
        } else {
            drag_zooming = false;
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
