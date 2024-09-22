#pragma once

#include "../gate.h"

class nand_gate : public gate {
public:
    nand_gate();
    void update() override;
    ~nand_gate() override;
    void draw(drawer& d) override;
};
