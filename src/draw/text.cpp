#include "text.h"
#include <Hd/Resource.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <array>

static auto res = Hd::Resource::getExeRes;

text_drawer::text_drawer(Hd::Camera& cam) : shader(res("res/shaders/text_vertex.glsl").first, res("res/shaders/text_texture.glsl").first), c(cam)
{

    float square[] = {
        -0.5f,  0.5f,
         0.5f,  0.5f,
        -0.5f, -0.5f,
         0.5f, -0.5f,
        };

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(square), square, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);

    stbtt_BakeFontBitmap((const unsigned char*)res("res/fonts/ElenaWebRegular.ttf").first,
        0, font_size, temp_bitmap, texture_size, texture_size, 32, 96, cdata);

    glGenTextures(1, &font_texture);
    glBindTexture(GL_TEXTURE_2D, font_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texture_size, texture_size, 0, GL_RED, GL_UNSIGNED_BYTE, temp_bitmap);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

}

text_drawer::~text_drawer()
{
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteTextures(1, &font_texture);
}

std::pair<glm::vec2, glm::vec2> text_drawer::get_text_size(std::string s, float fs) {

    glm::vec2 tl;
    glm::vec2 br;

    float cposx = 0;
    float cposy = 0;

    float r = fs / font_size;

    for (auto i = 0ul; i < s.size(); i++) {
        unsigned char c = s[i];
        int ix;

        if (c >= 32 && c < 128) {
            ix = c - 32;
        } else if (c == '\n') {
            cposx = 0;
            cposy -= font_size;
            continue;
        } else {
            ix = 0;
        }

        stbtt_bakedchar* b = cdata + ix;

        float p0x = cposx + b->xoff;
        float p0y = cposy + b->yoff;
        float p1x = cposx + b->xoff + (float)b->x1 - b->x0;
        float p1y = cposy + b->yoff - (float)b->y1 + b->y0;

        tl = {p0x < tl.x ? p0x : tl.x, p0y > tl.y ? p0y : tl.y};
        br = {p1x > br.x ? p1x : br.x, p1y < br.y ? p1y : br.y};

        cposx += b->xadvance;
    }
    return {r*tl, r*br};
}

void text_drawer::draw(std::string s, glm::vec2 pos, float size)
{
    float cposx = 0;
    float cposy = 0;

    float r = size / font_size;

    for (auto i = 0ul; i < s.size(); i++) {

        unsigned char c = s[i];

        int ix;

        if (c >= 32 && c <= 128) {
            ix = (c - 32);
        } else if (c == '\n') {
            cposx = pos.x;
            cposy -= font_size;
            continue;
        } else {
            ix = 0;
        }

        stbtt_bakedchar* b = cdata + ix;

        float tx0 = (float)b->x0 / texture_size;
        float ty0 = (float)b->y0 / texture_size;
        float tx1 = (float)b->x1 / texture_size;
        float ty1 = (float)b->y1 / texture_size;

        float cx = b->xoff + (b->x1 - b->x0)/2.0f;
        float cy = -b->yoff - (b->y1 - b->y0)/2.0f;
        float sx = b->x1 - b->x0;
        float sy = b->y1 - b->y0;

        glm::mat4 m(1.0f);
        m = glm::translate(m, { pos.x + r*(cposx+cx) , pos.y + r*(cposy+cy), 0.0f });
        m = glm::scale(m, { r*sx, r*sy, 1.0f });

        cposx += b->xadvance;

        std::array<float, 8> uniform;
        uniform[0] = tx0;
        uniform[1] = ty0;
        uniform[2] = tx1;
        uniform[3] = ty0;
        uniform[4] = tx0;
        uniform[5] = ty1;
        uniform[6] = tx1;
        uniform[7] = ty1;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindTexture(GL_TEXTURE_2D, font_texture);
        glBindVertexArray(vao);

        shader.Bind();

        glUniform2fv(2, 4, &uniform[0]);
        glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(*this->c.getVP()*m));

        glBindVertexArray(vao);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
}
