#pragma once

#include <glm/glm.hpp>

namespace logic::editor {

// 2d view of the world: zoom with the mouse wheel, pan with right drag
class canvas {
public:
    // size of the viewport in pixels, set every frame before drawing
    void set_size(glm::vec2 size);

    // mouse position in normalized device coordinates [-1, 1], y up
    void set_mouse(glm::vec2 ndc) { mouse = ndc; }

    // zoom and pan from imgui input. call only while the canvas is hovered
    void handle_view_input();

    const glm::mat4& vp() const { return m_vp; }
    float get_zoom() const { return zoom; }

    glm::vec2 to_world(glm::vec2 ndc) const;
    glm::vec2 mouse_world() const { return to_world(mouse); }

    // converts a distance in pixels to world units, for hit test tolerances
    float pixels(float px) const;

    // the same zoom as another view of the same world, and the same point
    // in its bottom left corner: views in the same window (editing and
    // simulating) share that corner, the simulation has a toolbar row more
    // above. takes effect with the next set_size.
    void copy_camera(const canvas& other)
    {
        zoom = other.zoom;
        center = other.center;
        anchor = other.to_world({-1.0f, -1.0f});
        anchored = true;
        recalc();
    }

private:
    void recalc();

    glm::mat4 m_vp{1.0f};
    glm::vec2 size{1.0f, 1.0f};
    glm::vec2 mouse{0.0f, 0.0f};

    glm::vec2 center{0.0f, 0.0f};
    float zoom = 0.01f;

    // keep this world point in the bottom left corner, see copy_camera
    glm::vec2 anchor{0.0f, 0.0f};
    bool anchored = false;

    bool panning = false;
    glm::vec2 pan_last{0.0f, 0.0f};
};

}
