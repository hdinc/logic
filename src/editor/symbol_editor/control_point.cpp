#include "control_point.h"

namespace logic::editor::symbol_editor {

control_point::control_point()
{
    set_box({1, 1});
    set_snap(false);
}

void control_point::draw(glm::mat4& vp, Hd::Graphics2D& g)
{
    g.setColor({1, 0, 0, 1});
    g.circle(vp, get_position(), 1);
}

}