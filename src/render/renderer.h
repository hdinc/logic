#pragma once

#include "../model/workspace.h"

#include <Hd/Graphics2D.h>
#include <Hd/Shader.h>
#include <Hd/VAO.h>

#include <string>

namespace logic::render {

namespace colors {
constexpr glm::vec4 symbol{1.0f, 1.0f, 1.0f, 1.0f};
constexpr glm::vec4 wire{0.7f, 0.7f, 0.7f, 1.0f};
constexpr glm::vec4 selection{1.0f, 1.0f, 0.0f, 1.0f};
constexpr glm::vec4 guide{0.5f, 0.6f, 0.1f, 1.0f};
constexpr glm::vec4 pin{0.3f, 0.5f, 1.0f, 1.0f};
constexpr glm::vec4 control_point{1.0f, 0.2f, 0.2f, 1.0f};
constexpr glm::vec4 error{1.0f, 0.25f, 0.25f, 1.0f};
constexpr glm::vec4 preview{0.6f, 0.6f, 0.6f, 1.0f};
}

enum class align {
    left,
    center,
    right
};

// owns the gl resources shared by all editors. needs a current gl context.
class renderer {
public:
    renderer();

    renderer(const renderer&) = delete;
    renderer& operator=(const renderer&) = delete;

    Hd::Graphics2D g;

    // dot grid over the whole viewport, faded out when zoomed out
    void grid(const glm::mat4& vp, float zoom);

    // pos is the left end of the baseline for align::left
    void text(const glm::mat4& vp, const std::string& str, glm::vec2 pos, float size, glm::vec4 color, align a = align::left);

private:
    Hd::Shader grid_dot_shader;
    Hd::VAO grid_vao;
};

void draw_shape(renderer& r, const glm::mat4& vp, const model::shape& s, glm::ivec2 pos, model::transform xf, glm::vec4 color);

// shapes and pins of a symbol placed at pos
void draw_symbol(renderer& r, const glm::mat4& vp, const model::symbol& s, glm::ivec2 pos, model::transform xf, glm::vec4 color);

// instance of a cell in a schematic. missing cells are drawn as a red box
void draw_instance(renderer& r, const glm::mat4& vp, const model::workspace& ws, const model::instance& i, glm::vec4 color);

// connector of a port inside its cell's schematic
void draw_port(renderer& r, const glm::mat4& vp, const model::port& p, glm::vec4 color);

void draw_wire(renderer& r, const glm::mat4& vp, const model::wire& w, glm::vec4 color);

}
