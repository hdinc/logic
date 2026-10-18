#pragma once

#include "../sim/simulator.h"

#include <algorithm>

namespace logic::editor {

// a running simulation of a top cell, shared by the views showing it. the
// cell editor of the top cell owns it, views of instances in other tabs
// close when it is stopped.
class sim_session {
public:
    // the cells of the hierarchy must not change while it runs
    sim_session(const model::workspace& ws, const model::cell& top);

    // ticks the clocks while running, called once per frame
    void update(float dt);

    bool uses(model::id cell) const;

    sim::simulator sim;
    model::id top;

    bool running = false;
    float ticks_per_second = 10.0f;

    // set when any view stops the simulation, the owner then drops it
    bool stopped = false;

private:
    double tick_accum = 0.0;
};

}
