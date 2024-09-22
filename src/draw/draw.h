#pragma once

#include <glm/glm.hpp>
#include <Hd.h>
#include "text.h"

class drawer {

public:
    drawer(Hd::Camera& c);
    drawer(const drawer&) = delete;
    drawer& operator=(const drawer&) = delete;
    ~drawer();

    void line(glm::vec2 p1, glm::vec2 p2);
    void circle(glm::vec2 pos, float r, bool fill);
    void rect(glm::vec2 pos, glm::vec2 dim, bool fill);
    void text(std::string s, glm::vec2 pos, float size);
    std::pair<glm::vec2, glm::vec2> get_text_size(std::string s, float fs);

    void set_color(float r, float g, float b, float a);

private:

    Hd::Shader lineShader;
    Hd::Shader circleShader;
    Hd::Shader rectShader;
    Hd::Camera& cam;

    text_drawer td;

    GLuint vao_line;

    GLuint vao_circle;
    GLuint vbo_circle;

    GLuint vao_rect;
    GLuint vbo_rect;
    GLuint ibo_rect;
};
