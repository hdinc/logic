#pragma once

#include <sys/types.h>

#include "net.h"
#include "gate.h"

struct pin {
    logic_value lv;
    net* n = &default_net;
    gate* g = 0;
    uint l = 1;

    void set_value(logic_value lv) {
        if (!(this->lv == lv)) {
            this->lv = lv;
            n->recalc();
        }
    }

    logic_value get_value() { return n->lv; }
};
