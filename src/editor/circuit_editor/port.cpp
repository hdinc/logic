#include "port.h"
#include <imgui.h>

namespace logic::editor::circuit_editor {

port::port(port_type type, int number, std::string name)
{
    this->number = number;
    this->type = type;
    this->name = name;

    set_box({5, 5});
    type_ = type;
    strncpy(name_buf, name.c_str(), 100);
}

void port::draw(glm::mat4& vp, Hd::Graphics2D& g)
{
    g.setColor({1, 1, 1, 1});
    if (type == port_type::input) {
        g.rect(vp, glm::vec2{get_position()} + glm::vec2{5, 5}, glm::vec2{get_position()} - glm::vec2{5, 5});
        g.line(vp, glm::vec2{get_position()} + glm::vec2{5, 0}, glm::vec2{get_position()} + glm::vec2{20, 0});
    } else {
        g.circle(vp, glm::vec2{get_position()}, 5.0f);
        g.line(vp, glm::vec2{get_position()} - glm::vec2{5, 0}, glm::vec2{get_position()} - glm::vec2{20, 0});
    }

    g.setText(name);
    g.setColor({1, 1, 1, 1});

    auto b = g.getTextBox();
    b.first *= 10.0f;
    b.second *= 10.0f;
    auto s = b.second - b.first;
    glm::vec2 p;
    if (type == port_type::input) {
        p = glm::vec2(get_position()) - (b.first + b.second) / 2.0f - glm::vec2{s.x / 2.0f + 8, 0};
    } else {
        p = glm::vec2(get_position()) - (b.first + b.second) / 2.0f + glm::vec2{s.x / 2.0f + 8, 0};
    }
    g.text(vp, p, 10.0f);
}

void port::property_editor()
{
    using namespace ImGui;

    if (Combo("Type", &type_, "input\0output\0inout\0")) {
        switch (type_) {
        case 0: type = port_type::input; break;
        case 1: type = port_type::output; break;
        case 2: type = port_type::inout; break;
        }
    }

    if (InputText("name", name_buf, 100, ImGuiInputTextFlags_EnterReturnsTrue)) {
        name = name_buf;
    }

    InputInt("Number", &number);
}

}