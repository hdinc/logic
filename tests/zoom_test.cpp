// zoom mode: one window filling the app, the others as tabs at the bottom

#include "../src/ui/zoom.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            std::printf("line %d: %s failed\n", __LINE__, #c);      \
            failures++;                                             \
        }                                                           \
    } while (0)

namespace zoom = logic::ui::zoom;

// a frame with windows A and B (B only if b_open), returns the windows
// whose content ran
static std::vector<std::string> frame(bool b_open = true)
{
    ImGui::NewFrame();
    std::vector<std::string> ran;
    ImGui::SetNextWindowPos({10, 10}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({200, 100}, ImGuiCond_FirstUseEver);
    if (zoom::begin("A")) ran.push_back("A");
    zoom::end();
    if (b_open) {
        ImGui::SetNextWindowPos({300, 10}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({200, 100}, ImGuiCond_FirstUseEver);
        if (zoom::begin("B###b")) ran.push_back("B");
        zoom::end();
    }
    zoom::end_frame();
    ImGui::Render();
    return ran;
}

int main()
{
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1280, 720};
    io.DeltaTime = 1.0f / 60;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    // not zoomed: both windows
    CHECK((frame() == std::vector<std::string>{"A", "B"}));
    CHECK(!zoom::active());

    // A zoomed: only A, filling the app above the tab bar
    zoom::set("A");
    frame();
    auto ran = frame();
    CHECK((ran == std::vector<std::string>{"A"}));
    auto win = ImGui::FindWindowByName("A###zoom_A");
    CHECK(win && win->Pos.x == 0 && win->Pos.y == 0 && win->Size.x == 1280 && std::abs(win->Size.y - (720 - zoom::bar_height())) < 0.5f);
    auto bar = ImGui::FindWindowByName("##zoom_bar");
    CHECK(bar && bar->Active && std::abs(bar->Pos.y - (720 - zoom::bar_height())) < 0.5f);
    // the normal windows aren't drawn
    auto a = ImGui::FindWindowByName("A");
    CHECK(a && !a->Active);

    // clicking B's tab zooms B: the tabs are A and B from the left
    float tab_y = 720 - zoom::bar_height() / 2;
    float b_x = -1;
    for (int k = 0; k < ImGui::GetCurrentContext()->TabBars.GetMapSize(); k++)
        if (auto tb = ImGui::GetCurrentContext()->TabBars.TryGetMapData(k); tb && tb->Tabs.Size == 2) b_x = (tb->Tabs[1].Offset + tb->BarRect.Min.x) + tb->Tabs[1].Width / 2;
    CHECK(b_x > 0);
    io.AddMousePosEvent(b_x, tab_y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    frame();
    CHECK(zoom::current() == "B###b");
    CHECK((frame() == std::vector<std::string>{"B"}));
    // its own window, not the one of the layout
    auto zb = ImGui::FindWindowByName("B###zoom_B___b");
    CHECK(zb && zb != ImGui::FindWindowByName("B###b"));

    // B closed while zoomed: back to the layout
    frame(false);
    CHECK(!zoom::active());
    CHECK((frame() == std::vector<std::string>{"A", "B"}));
    // the layout's windows keep their own place and size
    auto b = ImGui::FindWindowByName("B###b");
    CHECK(b && b->Size.x == 200 && b->Size.y == 100 && b->Pos.x == 300);

    // toggle zooms the window with focus, again unzooms
    ImGui::FocusWindow(ImGui::FindWindowByName("A"));
    frame();
    zoom::toggle();
    CHECK(zoom::current() == "A");
    zoom::toggle();
    CHECK(!zoom::active());
    CHECK((frame() == std::vector<std::string>{"A", "B"}));

    ImGui::DestroyContext();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
