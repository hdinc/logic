#include "wire.h"

void wire::draw(drawer& d) {

    d.set_color(0.5725, 0.5725, 0.5725, 1);

    // if (mode == circuit_mode::sim) {
    //     if (contention) {
    //         d.set_color(1, 0, 0, 1);
    //     } else if (!set) {
    //         d.set_color(0.2, 0.2, 0.2, 1);
    //     } else if (value) {
    //         d.set_color(1, 1, 0, 1);
    //     } else {
    //         d.set_color(0.5725, 0.5725, 0.5725, 1);
    //     }
    // } else if (mode == circuit_mode::edit) {
    // }


    d.rect(p1, {0.05, 0.05}, true);
    d.rect(p2, {0.05, 0.05}, true);
    d.line(p1, p2);

    // for (auto& i : segments) {

        // if (mode == circuit_mode::edit) {
        //     if (i.selected) d.set_color(0.6, 0.6, 0.6, 1);
        //     else d.set_color(0.3, 0.3, 0.3, 1);
        // }

    // }

    // for (auto& i : points) {
        // if (i.second > 2) {
        //     d.circle(i.first, 0.15, true);
        // }
    // // }
}
