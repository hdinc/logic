#pragma once

#include "renderer.h"

#include <vector>

namespace logic::render {

// what a display or an input part (led, 7-segment display, hex display,
// button, switch) shows while simulating, drawn over its symbol from the
// values at its ports. pins is empty while editing, then only the
// 7-segment display draws (its dark segments). nothing for other kinds.
// shown: which segments and point of a 7-segment display are lit, e.g.
// still lit for a while after they went dark. null: from the values.
void draw_live(renderer& r, const std::string& kind, const model::param_values& params, const model::instance& inst, const model::symbol& sym,
    const std::vector<model::port>& ports, const std::vector<sim::value>& pins, const bool* shown = nullptr);

}
