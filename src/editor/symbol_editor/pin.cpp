#include "pin.h"

#include "imgui.h"

namespace logic::editor::symbol_editor {

pin::pin()
{
    set_snap(true);
    set_box({2.5, 2.5});
    strncpy(name_buf, name.c_str(), 100);
}

void pin::draw(glm::mat4& vp, Hd::Graphics2D& g)
{
    g.setColor({0, 0, 0.8, 1});
    g.rect(vp, glm::vec2{get_position()} - glm::vec2{2.5, 2.5}, glm::vec2{get_position()} + glm::vec2{2.5, 2.5});
}

void pin::property_editor() {
    using namespace ImGui;

    InputInt("Number", &number, 1);
    if (InputText("Name", name_buf, 100, ImGuiInputTextFlags_EnterReturnsTrue)) {
        name = name_buf;
    }
}

}