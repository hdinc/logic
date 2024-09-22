#pragma once

#include "../gate.h"

class and_gate : public gate {
public:
    and_gate();
    void update() override;
    ~and_gate() override;
    void draw(drawer& d) override;
};
