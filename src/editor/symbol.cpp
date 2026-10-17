#include "symbol.h"

namespace logic::editor::symbol {
void symbol::draw(glm::mat4& vp, Hd::Graphics2D& g, glm::ivec2 pos)
{
    for (auto& l : lines) {
        g.setColor({1, 1, 1, 1});
        g.line(vp, pos + l.p1, pos + l.p2);
    }

    for (auto& b : beziers) {
        g.setColor({1, 1, 1, 1});

        auto pre = glm::vec2{b.p1};

        for (int k = 1; k <= 10; k++) {
            float t = (float)k / 10;
            glm::vec2 p = (1 - t) * ((1 - t) * glm::vec2{b.p1} + t * glm::vec2{b.p3}) + t * ((1 - t) * glm::vec2{b.p3} + t * glm::vec2{b.p2});
            g.line(vp, glm::vec2{pos} + pre, glm::vec2{pos} + p);
            pre = p;
        }
    }

    for (auto& c : circles) {
        g.setColor({1, 1, 1, 1});
        g.circle(vp, pos + c.p1, glm::length(glm::vec2{c.p2} - glm::vec2{c.p1}));
    }

    for (auto& t : texts) {
        g.setColor({1, 1, 1, 1});
        g.setText(t.t);
        g.text(vp, pos + t.pos, t.size);
    }

    // for (auto& p : pins) {
    // }
}
}