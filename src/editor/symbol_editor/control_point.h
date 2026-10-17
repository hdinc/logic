#pragma once

#include "../item.h"

namespace logic::editor::symbol_editor {

class control_point : public item {
public:
    control_point();
    void draw(glm::mat4& vp, Hd::Graphics2D& g) override;
};

}