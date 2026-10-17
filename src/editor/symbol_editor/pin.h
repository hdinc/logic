#pragma once

#include "../item.h"

namespace logic::editor::symbol_editor {

class pin : public item {
public:
    pin();
    void draw(glm::mat4& vp, Hd::Graphics2D& g) override;
    void property_editor() override;

    int number = 0;
    std::string name = "pin";
    char name_buf[100];
};

}