#include "types.h"

namespace logic::model {

glm::ivec2 transform::apply(glm::ivec2 p) const
{
    if (mirror) p.x = -p.x;

    switch (((rotation % 4) + 4) % 4) {
    case 1: return {-p.y, p.x};
    case 2: return {-p.x, -p.y};
    case 3: return {p.y, -p.x};
    default: return p;
    }
}

glm::vec2 transform::apply(glm::vec2 p) const
{
    if (mirror) p.x = -p.x;

    switch (((rotation % 4) + 4) % 4) {
    case 1: return {-p.y, p.x};
    case 2: return {-p.x, -p.y};
    case 3: return {p.y, -p.x};
    default: return p;
    }
}

bool rect::contains(glm::vec2 p) const
{
    return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
}

void rect::expand(glm::vec2 p)
{
    min = glm::min(min, p);
    max = glm::max(max, p);
}

void rect::expand(rect r)
{
    expand(r.min);
    expand(r.max);
}

rect make_rect(glm::vec2 a, glm::vec2 b)
{
    return {glm::min(a, b), glm::max(a, b)};
}

}
