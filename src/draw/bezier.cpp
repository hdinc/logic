#include "bezier.h"
#include <vector>

void draw_bezier(drawer& d, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3) {
    std::vector<glm::vec2> path;

    for(int i = 0; i <= 100; i++) {
        float t = i / 100.0f;
        auto p = p2 + (1-t)*(1-t)*(p1 - p2) + t*t*(p3-p2);
        path.push_back(p);
    }

    for (int i = 0; i < path.size() - 1; i++) {
        d.line(path[i], path[i+1]);
    }
}
