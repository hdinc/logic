#pragma once

#include "../item.h"
#include "../symbol.h"

namespace logic::editor::circuit_editor {

class component : public item {

public:
    virtual void draw(glm::mat4& vp, Hd::Graphics2D& g) override;

    std::string library_name;
    std::string name;

    symbol::symbol* symbol = 0;
};

}