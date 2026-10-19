#include "sim_session.h"

namespace logic::editor {

sim_session::sim_session(const model::workspace& ws, const model::cell& c, const sim::component_factory& factory)
    : run(std::make_unique<sim::simulator>(ws, c, factory))
    , top(c.id)
{
}

bool sim_session::uses(model::id cell) const
{
    auto& c = run.sim().cells();
    return std::find(c.begin(), c.end(), cell) != c.end();
}

}
