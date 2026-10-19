#pragma once

#include "../sim/runner.h"

#include <algorithm>

namespace logic::editor {

// a running simulation of a top cell, shared by the views showing it. the
// cell editor of the top cell owns it, views of instances in other tabs
// close when it is stopped. the simulation runs on its own thread, views
// draw the values of the last update().
class sim_session {
public:
    // the cells of the hierarchy must not change while it runs
    sim_session(const model::workspace& ws, const model::cell& top);

    // takes the latest values from the simulation thread, once per frame
    void update() { run.sync(); }

    bool uses(model::id cell) const;

    // the values to draw
    const sim::snapshot& sim() const { return run.view(); }

    sim::runner run;
    model::id top;

    // set when any view stops the simulation, the owner then drops it
    bool stopped = false;
};

}
