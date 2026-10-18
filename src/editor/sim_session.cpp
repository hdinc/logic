#include "sim_session.h"
#include "../builtin/builtin.h"

namespace logic::editor {

sim_session::sim_session(const model::workspace& ws, const model::cell& c)
    : sim(ws, c, builtin::factory())
    , top(c.id)
{
}

void sim_session::update(float dt)
{
    if (!running || stopped) {
        tick_accum = 0.0;
        return;
    }

    tick_accum += dt * ticks_per_second;
    // don't fall behind forever when ticks are slow
    int n = (int)std::min(tick_accum, 1000.0);
    tick_accum = n == 1000 ? 0.0 : tick_accum - n;
    for (int i = 0; i < n && running; i++) {
        sim.tick();
        if (sim.oscillating()) running = false;
    }
}

bool sim_session::uses(model::id cell) const
{
    auto& c = sim.cells();
    return std::find(c.begin(), c.end(), cell) != c.end();
}

}
