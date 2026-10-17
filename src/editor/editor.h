#pragma once

#include <Hd.h>
#include <memory>

namespace logic {
namespace editor {

class editor {
public:
    editor();

    void process_inputs();

    void draw_grid();

    void recalc_vp();

    glm::vec2 screen_to_world(glm::vec2 mpos);
    glm::vec2 world_to_screen(glm::vec2 wpos);

public:
    glm::mat4 vp;
    glm::vec2 fbsize{0.0f, 0.0f};
    glm::vec2 mpos;

    std::shared_ptr<Hd::Graphics2D> g;

private:
    glm::vec2 center{0.0f, 0.0f};
    float zoom = 0.01f;

private:
    static std::weak_ptr<Hd::VAO> s_grid_vao;
    static std::weak_ptr<Hd::VBO> s_grid_vbo;
    static std::weak_ptr<Hd::Shader> s_grid_shader;
    static std::weak_ptr<Hd::Shader> s_grid_dot_shader;
    static std::weak_ptr<Hd::Graphics2D> s_g;

    std::shared_ptr<Hd::VAO> grid_vao;
    std::shared_ptr<Hd::VBO> grid_vbo;
    std::shared_ptr<Hd::Shader> grid_shader;
    std::shared_ptr<Hd::Shader> grid_dot_shader;
};

}
}