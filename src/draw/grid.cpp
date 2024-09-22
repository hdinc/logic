#include "grid.h"

grid::grid(class drawer& d) : d(d) {}

void grid::draw()
{
    d.set_color(0.0784, 0.0784, 0.0784, 1);

    for (int i = 0; i < 101; i++) {
        glm::vec2 p1 = { (i - 50), -50 };
        glm::vec2 p2 = { (i - 50), 50 };
        d.line(p1, p2);
    }
    for (int i = 0; i < 101; i++) {
        glm::vec2 p1 = { -50, (i - 50) };
        glm::vec2 p2 = { 50, (i - 50)  };
        d.line(p1, p2);
    }
}
