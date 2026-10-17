#include "circle.h"

namespace logic::editor::symbol_editor {

circle::circle(control_point* p1, control_point* p2)
{
    this->p1 = p1;
    this->p2 = p2;
}

void circle::draw(glm::mat4& vp, Hd::Graphics2D& g)
{
    g.setColor({1, 1, 1, 1});
    g.circle(vp, p1->get_position(), glm::length(glm::vec2{p1->get_position()} - glm::vec2{p2->get_position()}));
}

}
