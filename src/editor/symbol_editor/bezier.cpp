#include "bezier.h"

namespace logic::editor::symbol_editor {

bezier::bezier(control_point* p1, control_point* p2, control_point* p3)
{
    this->p1 = p1;
    this->p2 = p2;
    this->p3 = p3;
}

void bezier::draw(glm::mat4& vp, Hd::Graphics2D& g)
{
    g.setColor({1, 1, 1, 1});

    auto pre = glm::vec2{p1->get_position()};

    for (int k = 1; k <= n; k++) {
        float t = (float)k / n;
        glm::vec2 p = (1 - t) * ((1 - t) * glm::vec2{p1->get_position()} + t * glm::vec2{p3->get_position()}) + t * ((1 - t) * glm::vec2{p3->get_position()} + t * glm::vec2{p2->get_position()});
        g.line(vp, pre, p);
        pre = p;
    }
}

}