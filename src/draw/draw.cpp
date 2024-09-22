#include "draw.h"
#include <glm/gtc/type_ptr.hpp>
#include <vector>

auto res = Hd::Resource::getExeRes;

drawer::drawer(Hd::Camera& c)
    : lineShader   (res("res/line.glsl"  ).first, res("res/frag.glsl").first)
    , circleShader (res("res/circle.glsl").first, res("res/frag.glsl").first)
    , rectShader   (res("res/rect.glsl"  ).first, res("res/frag.glsl").first)
    , cam(c)
    , td(c)
{
    // line
    {
        glGenVertexArrays(1, &vao_line);
        glBindVertexArray(vao_line);
    }

    // circle
    {
        glGenVertexArrays(1, &vao_circle);
        glBindVertexArray(vao_circle);
        glGenBuffers(1, &vbo_circle);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_circle);

        std::vector<glm::vec2> b;
        b.push_back({0,0});
        for (int i = 0; i < 30; i++) {
            float a = i / 30.0f * 2 * glm::pi<float>();
            b.push_back({sin(a), cos(a)});
        }
        b.push_back({0, 1});

        glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec2) * b.size(), glm::value_ptr(b[0]), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, false, 0, 0);
    }

    // rect
    {
        glGenVertexArrays(1, &vao_rect);
        glBindVertexArray(vao_rect);
        glGenBuffers(1, &vbo_rect);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_rect);
        glGenBuffers(1, &ibo_rect);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo_rect);

        float b[4][2];
        b[0][0] = -1; b[0][1] = -1;
        b[1][0] = -1; b[1][1] =  1;
        b[2][0] =  1; b[2][1] =  1;
        b[3][0] =  1; b[3][1] = -1;

        unsigned int indexb[4] = {0, 1, 3, 2};

        glBufferData(GL_ARRAY_BUFFER, sizeof(b), b, GL_STATIC_DRAW);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indexb), indexb, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, false, 0, 0);
    }
};

drawer::~drawer() {
    glBindVertexArray(0);

    glDeleteVertexArrays(1, &vao_line);

    glDeleteVertexArrays(1, &vao_circle);
    glDeleteBuffers(1, &vbo_circle);

    glDeleteVertexArrays(1, &vao_rect);
    glDeleteBuffers(1, &vbo_rect);
    glDeleteBuffers(1, &ibo_rect);
}

void drawer::line(glm::vec2 p1, glm::vec2 p2) {
    glBindVertexArray(vao_line);
    lineShader.Bind();
    glm::vec2 p[2] = { p1, p2 };
    glUniformMatrix4fv(0, 1, false, glm::value_ptr(*cam.getVP()));
    glUniform2fv(1, 2, glm::value_ptr(p[0]));
    glDrawArrays(GL_LINES, 0, 2);
}

void drawer::circle(glm::vec2 pos, float r, bool fill) {
    glBindVertexArray(vao_circle);
    circleShader.Bind();
    glm::mat4 m{1.0f};
    m = glm::translate(m, {pos.x, pos.y, 0});
    m = glm::scale(m, {r, r, 1});
    auto mvp = *cam.getVP() * m;
    glUniformMatrix4fv(0, 1, false, glm::value_ptr(mvp));

    if(fill) {
        glDrawArrays(GL_TRIANGLE_FAN, 0, 32);
    }
    else {
        glDrawArrays(GL_LINE_LOOP, 1, 31);
    }
}

void drawer::rect(glm::vec2 pos, glm::vec2 dim, bool fill) {
    glBindVertexArray(vao_rect);
    rectShader.Bind();
    glm::mat4 m{1.0f};
    m = glm::translate(m, {pos.x, pos.y, 0});
    m = glm::scale(m, {dim.x, dim.y, 1});
    auto mvp = *cam.getVP() * m;
    glUniformMatrix4fv(0, 1, false, glm::value_ptr(mvp));

    if(fill) {
        glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_INT, 0);
    }
    else {
        glDrawArrays(GL_LINE_LOOP, 0, 4);
    }
}

void drawer::set_color(float r, float g, float b, float a) {
    lineShader.Bind();
    glUniform4f(5, r, g, b, a);
    circleShader.Bind();
    glUniform4f(5, r, g, b, a);
    rectShader.Bind();
    glUniform4f(5, r, g, b, a);
}

void drawer::text(std::string s, glm::vec2 pos, float size) {
    td.draw(s, pos, size);
}


std::pair<glm::vec2, glm::vec2> drawer::get_text_size(std::string s, float fs) {
    return td.get_text_size(s, fs);
}
