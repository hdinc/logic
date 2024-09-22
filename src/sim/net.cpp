#include "net.h"
#include "pin.h"

#include "common.h"

net default_net;

void net::recalc() {

    uint z = -1;
    uint v = 0;

    for (auto p : pins) {
        uint z1 = p->lv.z;
        uint v1 = p->lv.v;

        if ((~(z | z1) & v) ^ (~(z | z1) & v1)) {
            x = 1;
            throw logic_contention_exception{};
        }

        v = (~(z ^ z1) & v) | ((z ^ z1) & ((~z & v) | (~z1 & v1)));
        z &= z1;
    }

    if ((lv.z != z) || ((~lv.z & lv.v) != (~z & v))) {
        for (auto p : pins) {
            p->g->need_update = true;
        }
    }

    lv.z = z;
    lv.v = v;

}
