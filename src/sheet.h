#pragma once

#include <imgui.h>
#include <vector>
#include "draw/grid.h"
#include "ui_elements/ui_element.h"
#include "draw/draw.h"
#include <Hd.h>

class sheet {
public:
    sheet(Hd::Camera& c);
    ~sheet();

    void gui();
    void draw();
    void process_inputs();

private:

    void select_element(glm::vec2 loc);
    glm::vec2 select_diff;

    std::vector<ui_element*> elements;
    ui_element* selected_element = 0;

    Hd::Camera& cam;
    drawer d;

    grid g;
};
