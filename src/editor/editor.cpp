#include "editor.h"
#include "glm/gtc/type_ptr.hpp"

namespace logic {
namespace editor {

std::weak_ptr<Hd::Shader> editor::s_grid_shader;
std::weak_ptr<Hd::Shader> editor::s_grid_dot_shader;
std::weak_ptr<Hd::VAO> editor::s_grid_vao;
std::weak_ptr<Hd::VBO> editor::s_grid_vbo;
std::weak_ptr<Hd::Graphics2D> editor::s_g;

void editor::process_inputs()
{

    using namespace ImGui;
    auto io = GetIO();

    glm::vec2 old_mpos;

    if (io.MouseWheel != 0) {
        auto p1 = screen_to_world(mpos);
        zoom *= 1 + io.MouseWheel / 10.0f;
        zoom = glm::clamp(zoom, 1.0f / 10000.0f, 1.0f);
        recalc_vp();
        auto p2 = screen_to_world(mpos);
        center += p1 - p2;
        recalc_vp();
    }

    if (IsMouseClicked(ImGuiMouseButton_Right)) {
        old_mpos = mpos;
    } else if (IsMouseDown(ImGuiMouseButton_Right)) {
        center += screen_to_world(old_mpos) - screen_to_world(mpos);
        recalc_vp();
        old_mpos = mpos;
    }
}

glm::vec2 editor::screen_to_world(glm::vec2 mpos)
{
    auto r = glm::inverse(vp) * glm::vec4(mpos.x, mpos.y, 0.0f, 1.0f);
    return {r.x, r.y};
}

glm::vec2 editor::world_to_screen(glm::vec2 wpos)
{
    auto r = vp * glm::vec4(wpos.x, wpos.y, 0.0f, 1.0f);
    return {r.x, r.y};
}

editor::editor()
{

    if (s_grid_shader.use_count() == 0) {
        grid_shader = std::make_shared<Hd::Shader>(Hd::Resource::getExeRes("res/shaders/grid_vert.glsl").first, Hd::Resource::getExeRes("res/shaders/grid_frag.glsl").first);
        s_grid_shader = grid_shader;
    } else {
        grid_shader = s_grid_shader.lock();
    }

    if (s_grid_dot_shader.use_count() == 0) {
        grid_dot_shader = std::make_shared<Hd::Shader>(Hd::Resource::getExeRes("res/shaders/grid_dot_vert.glsl").first, Hd::Resource::getExeRes("res/shaders/grid_dot_frag.glsl").first);
        s_grid_dot_shader = grid_dot_shader;
    } else {
        grid_dot_shader = s_grid_dot_shader.lock();
    }

    if (s_grid_vao.use_count() == 0) {
        grid_vao = std::make_shared<Hd::VAO>();
        grid_vbo = std::make_shared<Hd::VBO>();

        s_grid_vao = grid_vao;
        s_grid_vbo = grid_vbo;

        grid_vao->Bind();
        grid_vbo->Bind(GL_ARRAY_BUFFER);
        std::vector<glm::vec2> d;

        for (int i = -500; i <= 500; i++) {
            glm::vec2 p1 = {i * 10, -500};
            glm::vec2 p2 = {i * 10, 500};
            d.push_back(p1);
            d.push_back(p2);
        }

        for (int i = -500; i <= 500; i++) {
            glm::vec2 p1 = {-500, i * 10};
            glm::vec2 p2 = {500, i * 10};
            d.push_back(p1);
            d.push_back(p2);
        }

        glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec2) * d.size(), &d[0], GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);

    } else {
        grid_vao = s_grid_vao.lock();
        grid_vbo = s_grid_vbo.lock();
    }

    if (s_g.use_count() == 0) {
        g = std::make_shared<Hd::Graphics2D>();
        s_g = g;
    } else {
        g = s_g.lock();
    }
}

void editor::recalc_vp()
{
    glm::mat4 r(1.0f);
    glm::vec3 s(zoom);

    if (fbsize.x > fbsize.y) {
        s.y *= fbsize.x / fbsize.y;
    } else {
        s.x *= fbsize.y / fbsize.x;
    }

    r = glm::scale(r, s);
    r = glm::translate(r, {-center, 0.0f});
    vp = r;
}

void editor::draw_grid()
{
    // if (zoom < 1.0 / 1000.0) return;

    float c = glm::smoothstep(1.0f / 1000, 1.0f / 300, zoom) * 0.3;
    glm::vec4 color = {c, c, c, 1.0};

    // grid_shader->Bind();
    grid_dot_shader->Bind();
    glUniformMatrix4fv(0, 1, false, glm::value_ptr(vp));
    glUniform4fv(1, 1, &color.x);

    grid_vao->Bind();
    // glDrawArrays(GL_LINES, 0, 4004);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

}
}