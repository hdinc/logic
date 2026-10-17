#include "text.h"
#include "imgui.h"

namespace logic::editor::symbol_editor {
text::text()
{
    set_snap(false);
    set_box({2.5, 2.5});

    t_buf[0] = 0;
    strncpy(t_buf, t.c_str(), 100);
}

void text::draw(glm::mat4& vp, Hd::Graphics2D& g)
{
    g.setColor({1, 1, 1, 1});
    g.setText(t);
    g.text(vp, get_position(), size);
    g.setColor({1,0,0,1});
    g.circle(vp, get_position(), 1);
}

void text::property_editor()
{
    using namespace ImGui;

    if (InputText("name", t_buf, 100, ImGuiInputTextFlags_EnterReturnsTrue)) {
        t = t_buf;
    }

    SliderFloat("size", &size, 1, 20);
}

}