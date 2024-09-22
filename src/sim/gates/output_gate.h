
#pragma once

#include "../gate.h"

class output_gate : public gate {
public:
    output_gate();
    void update() override;
    ~output_gate() override;
    void draw(drawer& d) override;

    uint val = 0;
};
