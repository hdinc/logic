#pragma once

#include <vector>
#include "wire.h"
#include <sys/types.h>
#include <set>

struct pin;

struct logic_value {
    bool operator==(const logic_value& other) { return other.v == v && other.z == z; }
    uint v = 0;
    uint z = -1;
};

class net {
public:
    void recalc();

    std::set<pin*> pins;
    std::set<wire*> wires;

    std::set<glm::ivec2> positions;

    logic_value lv;

    uint x = 0;
    uint l = 1;
};

extern net default_net;
