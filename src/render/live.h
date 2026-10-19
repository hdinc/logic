#pragma once

#include "renderer.h"

#include <vector>

namespace logic::render {

// what a display or an input part (led, 7-segment display, hex display,
// button, switch) shows while simulating, drawn over its symbol from the
// values at its pins. nothing for other kinds.
void draw_live(renderer& r, const std::string& kind, const model::param_values& params, const model::instance& inst, const model::symbol& sym,
    const std::vector<sim::value>& pins);

}
