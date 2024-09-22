#pragma once

#include "../gate.h"

class or_gate : public gate {
public:
    or_gate();
    void update() override;
    ~or_gate() override;
    void draw(drawer& d) override;
};
