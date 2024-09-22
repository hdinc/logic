#include "sheet.h"
#include <string>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>

enum class input_state {idle, drag};
static input_state istate = input_state::idle;

static glm::vec2 getMouseWorldCoord(Hd::Camera& cam) {
    auto io = ImGui::GetIO();
    glm::vec2 mloc = glm::vec2{io.MousePos.x / io.DisplaySize.x, io.MousePos.y / io.DisplaySize.y};
    mloc = (mloc - glm::vec2{0.5, 0.5}) * 2.0f;
    mloc.y = -mloc.y;
    mloc = glm::inverse(*cam.getVP()) * glm::vec4(mloc.x, mloc.y, 0, 1);
    return mloc;
}

sheet::sheet(Hd::Camera& c) : cam(c) , d(c), g(d)
{
}

sheet::~sheet() {
    for (auto i : elements) {
        delete i;
    }
}

void sheet::process_inputs() {
    auto io = ImGui::GetIO();

    glm::vec2 mloc = getMouseWorldCoord(cam);

    static glm::vec2 mouse_press_loc {0,0};

    if(io.MouseClicked[0]) {
        mouse_press_loc = mloc;
        select_element(mloc);
        if(selected_element) {
            istate = input_state::drag;
        }
    }

    if(io.MouseReleased[0]) {
        istate = input_state::idle;
    }

    if(istate==input_state::drag) {
        glm::vec2 l = mloc - select_diff;
        selected_element->move({std::round(10*l.x)/10.0f, std::round(10*l.y)/10.0f});
    }

    // TODO: efficient delete, swap with end and remove from end without preserving order
    if(selected_element) {
        if (ImGui::IsKeyPressed(ImGuiKey_X)) {
            auto it = std::find(elements.begin(),elements.end(),selected_element);
            if (it != elements.end()) {
                elements.erase(it);
            }
            selected_element = 0;
        }
    }
}

void sheet::draw() {
    g.draw();
    for(auto& i : elements) {
        i->draw(d);
    }
}

void sheet::gui() {
    using namespace ImGui;

    Begin("Sheet");

    Text("input_state = %s", istate == input_state::drag ? "drag" : "idle");

    auto mloc = getMouseWorldCoord(cam);
    Text("loc = %.2f, %.2f", mloc.x, mloc.y);

    if(Button("add anchor")) {
        elements.push_back(new anchor());
    }

    if(Button("add pin")) {
        elements.push_back(new pin());
    }

    if(Button("add line")) {
        elements.push_back(new line());
    }

    if(Button("add spline")) {
        elements.push_back(new spline());
    }

    // for(auto i = 0ul; i < gates.size(); i++) {
    //     SliderFloat2(("gate " + std::to_string(i)).c_str(), glm::value_ptr(gates[i]->loc), -10, 10);
    // }

    for (ui_element* i : elements) {
        Text("%s", i->desc().c_str());
    }
    End();
}

void sheet::select_element(glm::vec2 loc){

    bool b = false;

    for (auto i = elements.begin(); i != elements.end(); ++i) {
        if((*i)->try_select(loc)) {
            b = true;
            if(selected_element && selected_element != (*i)) selected_element->selected = false;
            selected_element = (*i);
            select_diff = loc - (*i)->get_selection_loc();
            iter_swap(i, elements.end() - 1);
            break;
        };
    }

    if(!b) {
        if(selected_element) {
            selected_element->selected = false;
            selected_element = 0;
        }
    }
}
