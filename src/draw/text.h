#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "stb_truetype.h"
#include <array>
#include <Hd.h>

class text_drawer {
public:
    text_drawer(Hd::Camera& cam);
    ~text_drawer();
    void draw(std::string s, glm::vec2 pos, float size);
    std::pair<glm::vec2, glm::vec2> get_text_size(std::string s, float fs);

private:
    Hd::Shader shader;
    GLuint font_texture;
    GLuint vao, vbo;

    int font_size = 72;

    static constexpr int texture_size = 2048;
    unsigned char temp_bitmap[texture_size * texture_size];
    stbtt_bakedchar cdata[96]; // ASCII 32..126 95 glyphs

    std::vector<glm::vec2> dlocs;
    std::vector<glm::vec2> sizes;

    Hd::Camera& c;
};

