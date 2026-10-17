#include "wire.h"

namespace logic::editor::circuit_editor {

void wire_point::draw(glm::mat4& vp, Hd::Graphics2D& g)
{
    g.setColor({.7, .7, .7, 1});
    g.rect(vp, glm::vec2{get_position()} + glm::vec2{-0.75, -0.75}, glm::vec2{get_position()} + glm::vec2{0.75, 0.75}, true);
}

wire::wire(wire_point* p1, wire_point* p2)
{
    this->p1 = p1;
    this->p2 = p2;
}

void wire::draw(glm::mat4& vp, Hd::Graphics2D& g)
{
    p1->draw(vp, g);
    p2->draw(vp, g);
    g.setColor({.7, .7, .7, 1});
    g.line(vp, p1->get_position(), p2->get_position());
}

}