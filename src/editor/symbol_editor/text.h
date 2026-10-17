#pragma once

#include "../item.h"

namespace logic::editor::symbol_editor {

class text : public item {
public:
    text();
    void draw(glm::mat4& vp, Hd::Graphics2D& g) override;

    void property_editor() override;

    float size = 10.0f;

    std::string t = "Text";
    char t_buf[100];
};

}
