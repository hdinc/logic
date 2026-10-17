#include "line.h"

namespace logic::editor::symbol_editor {

line::line(control_point* p1, control_point* p2)
{
    this->p1 = p1;
    this->p2 = p2;
}

void line::draw(glm::mat4& vp, Hd::Graphics2D& g)
{
    g.setColor({1, 1, 1, 1});
    g.line(vp, p1->get_position(), p2->get_position());
}

}