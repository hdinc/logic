#include "component.h"

namespace logic::editor::circuit_editor {

void component::draw(glm::mat4& vp, Hd::Graphics2D& g)
{
    symbol->draw(vp, g, get_position());
}

}