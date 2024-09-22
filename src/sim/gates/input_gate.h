#pragma once

#include "../gate.h"

class input_gate : public gate {
public:
    input_gate();
    void update() override;
    ~input_gate() override;
    void draw(drawer& d) override;

    uint val = 0;
};
