#include "item.h"

namespace logic {
namespace editor {

bool item::try_select(glm::vec2 mpos)
{
    return (glm::abs(mpos.x - position.x) < box.x && glm::abs(mpos.y - position.y) < box.y);
}

bool item::is_inside_rect(glm::vec2 p1, glm::vec2 p2)
{

    float left, right, up, down;
    if (p1.x > p2.x) {
        left = p2.x;
        right = p1.x;
    } else {
        left = p1.x;
        right = p2.x;
    }

    if (p1.y > p2.y) {
        down = p2.y;
        up = p1.y;
    } else {
        down = p1.y;
        up = p2.y;
    }

    return (position.x > left && position.x < right && position.y > down && position.y < up);
}

void item::property_editor() { }

}
}