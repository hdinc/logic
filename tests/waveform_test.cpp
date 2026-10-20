#include "../src/builtin/builtin.h"
#include "../src/editor/waveform_view.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdio>

using namespace logic;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("line %d: %s failed\n", __LINE__, #cond); failures++; } } while (0)

static void test_formats()
{
    using editor::waveform_format;
    auto v = sim::known(255, 8);
    CHECK(editor::format_waveform_value(v, waveform_format::hex) == "0xFF");
    CHECK(editor::format_waveform_value(v, waveform_format::uint) == "255");
    CHECK(editor::format_waveform_value(v, waveform_format::sint) == "-1");
    CHECK(editor::format_waveform_value(sim::known(128, 8), waveform_format::sint) == "-128");
    CHECK(editor::format_waveform_value(v, waveform_format::binary) == "0b11111111");
    CHECK(editor::format_waveform_value(v, waveform_format::octal) == "0o377");
    CHECK(editor::format_waveform_value(sim::floating(8), waveform_format::hex) == "0xzz");
    CHECK(editor::format_waveform_value(sim::unknown(8), waveform_format::uint) == "xxxxxxxx");
    auto wide = sim::known(0, 70);
    wide.set(69, sim::bit_state::one);
    CHECK(editor::format_waveform_value(wide, waveform_format::uint) == "590295810358705651712");
    CHECK(editor::format_waveform_value(wide, waveform_format::sint) == "-590295810358705651712");
    wide.set_word(0, UINT64_MAX, 0, 0);
    wide.set_word(1, 0x3F, 0, 0);
    CHECK(editor::format_waveform_value(wide, waveform_format::uint) == "1180591620717411303423");
    CHECK(editor::format_waveform_value(wide, waveform_format::sint) == "-1");
    CHECK(editor::waveform_signal_name("top/U1.Q", true) == "top/U1.Q");
    CHECK(editor::waveform_signal_name("top/U1.Q", false) == "Q");
    CHECK(editor::waveform_signal_name("top/clock", false) == "clock");
}

static void test_timeline()
{
    editor::waveform_timeline timeline;
    timeline.fit(10 * sim::ms, 20 * sim::ms);
    auto span = timeline.span();
    auto anchor = timeline.start() + span * 0.25;
    timeline.zoom(2, 0.25);
    CHECK(timeline.span() == span); // wheel input queues an animation
    timeline.update(1.0 / 60, 20 * sim::ms, false);
    CHECK(timeline.span() < span && timeline.span() > span / (1.25 * 1.25));
    CHECK(std::abs(timeline.start() + timeline.span() * 0.25 - anchor) < 0.01);
    CHECK(timeline.follow); // zooming while paused also preserves the option
    for (int n = 0; n < 120; n++) timeline.update(1.0 / 60, 20 * sim::ms, false);
    CHECK(std::abs(timeline.span() - span / (1.25 * 1.25)) < 0.01);
    timeline.update(1.0 / 60, 30 * sim::ms, true);
    timeline.zoom(3, 0.7);
    timeline.update(1.0 / 60, 31 * sim::ms, true);
    CHECK(timeline.follow);
    CHECK(std::abs((31 * sim::ms - timeline.start()) / timeline.span() - 0.95) < 1e-9);
    span = timeline.span();
    timeline.pan(0.1);
    CHECK(!timeline.follow);
    timeline.update(1, 31 * sim::ms, false);
    CHECK(timeline.span() == span); // panning cancels an outstanding animation
}

// Exercise the real ImGui window without a native window or GL context.
// This catches invalid draw coordinates and widget/frame lifecycle errors.
int main()
{
    test_formats();
    test_timeline();
    model::workspace ws;
    ws.add_builtin_library(builtin::registry());
    auto& cell = ws.create_cell(ws.add_library("work"), "top");
    cell.add_port(model::direction::input, {-20, 0}).width = 8;
    cell.add_port(model::direction::output, {20, 0}).width = 8;
    for (int k = 1; k <= 10; k++) {
        cell.add_port(model::direction::input, {-20, 40 * k});
        cell.add_port(model::direction::output, {20, 40 * k});
    }
    sim::runner run(std::make_unique<sim::simulator>(ws, cell, builtin::factory()));
    int net = run.sim().net_of_port(run.sim().root(), 0);
    run.post([net](sim::simulator& s) {
        s.waveforms.add(s, net, "top/input");
        for (int k = 1; k <= 10; k++)
            s.waveforms.add(s, s.net_of_port(s.root(), 2 * k), "top/input" + std::to_string(k));
        s.set_input(0, sim::known(0xF0, 8));
        s.run_until(sim::ms);
        s.set_input(0, sim::known(0xF1, 8));
        s.run_until(2 * sim::ms);
        s.set_input(0, sim::known(0x7F, 8));
        s.run_until(3 * sim::ms);
    });
    run.wait_idle();

    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1280, 720};
    io.DeltaTime = 1.0f / 60;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    editor::waveform_view view;
    view.visible = true;
    bool drew = false, filled_bus = false;
    auto draw_frame = [&](ImVec2 size = {1000, 420}) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({20, 20});
        ImGui::SetNextWindowSize(size);
        view.gui(run, "top", cell.id);
        ImGui::Render();
        auto data = ImGui::GetDrawData();
        drew |= data->TotalVtxCount > 0;
        for (auto list : data->CmdLists)
            for (auto& vertex : list->VtxBuffer) {
                filled_bus |= vertex.col == ImGui::GetColorU32(ImVec4{80.0f / 255, 200.0f / 255, 130.0f / 255, 0.18f});
                CHECK(std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y));
            }
    };
    for (int frame = 0; frame < 16; frame++) {
        // Zoom, scrub, and pan, then shrink the window and reset history.
        io.AddMousePosEvent(650, 240);
        if (frame == 2) io.AddMouseWheelEvent(0, 2);
        if (frame == 4) io.AddMouseButtonEvent(0, true);
        if (frame == 5) io.AddMouseButtonEvent(0, false);
        if (frame == 6) io.AddMouseButtonEvent(2, true);
        if (frame == 7) io.AddMousePosEvent(600, 240);
        if (frame == 8) io.AddMouseButtonEvent(2, false);
        if (frame == 9) {
            io.AddMousePosEvent(70, 240);
            io.AddMouseWheelEvent(0, -3);
        }
        if (frame == 12) {
            run.reset();
            run.wait_idle();
        }
        draw_frame(frame < 10 ? ImVec2{1000, 420} : ImVec2{240, 260});
    }

    // Exercise bus expansion and the divider through real mouse events.
    draw_frame();
    auto label = "Waveforms: top###waveforms" + std::to_string(cell.id);
    auto parent = ImGui::FindWindowByName(label.c_str());
    CHECK(parent && parent->DC.ChildWindows.Size == 1);
    if (!parent || parent->DC.ChildWindows.Size != 1) return 1;
    auto plot = parent->DC.ChildWindows[0];
    ImGui::SetScrollY(plot, 0);
    draw_frame();
    float x = plot->Pos.x + plot->WindowPadding.x;
    float y = plot->Pos.y + plot->WindowPadding.y;
    float axis_height = ImGui::GetTextLineHeightWithSpacing() * 2;
    float arrow_center = x + ImGui::CalcTextSize("x").x + 4 + 2 + (ImGui::GetFontSize() + 2) / 2;
    io.AddMousePosEvent(arrow_center, y + axis_height + 3 + (ImGui::GetFontSize() + 2) / 2);
    draw_frame();
    float scroll_before = plot->ScrollMax.y;
    io.AddMouseButtonEvent(0, true);
    draw_frame();
    io.AddMouseButtonEvent(0, false);
    draw_frame();
    draw_frame();
    draw_frame();
    CHECK(plot->ScrollMax.y > scroll_before + 200); // eight new bit rows

    io.AddMousePosEvent(x + 253, y + 10);
    draw_frame();
    CHECK(ImGui::GetMouseCursor() == ImGuiMouseCursor_ResizeEW);
    io.AddMouseButtonEvent(0, true);
    draw_frame();
    io.AddMousePosEvent(x + 303, y + 10);
    draw_frame();
    bool moved_divider = false;
    for (auto& vertex : plot->DrawList->VtxBuffer)
        moved_divider |= vertex.col == ImGui::GetColorU32(ImGuiCol_SeparatorActive) && std::abs(vertex.pos.x - (x + 300)) < 1;
    CHECK(moved_divider);
    io.AddMouseButtonEvent(0, false);
    draw_frame();

    io.AddMousePosEvent(x + 100, y + axis_height + 10);
    draw_frame();
    io.AddMouseButtonEvent(1, true);
    draw_frame();
    io.AddMouseButtonEvent(1, false);
    draw_frame();
    CHECK(ImGui::GetCurrentContext()->OpenPopupStack.Size > 0); // per-signal format menu
    ImGui::DestroyContext();
    CHECK(drew && filled_bus);
    if (failures) return 1;
    std::puts("waveform window frames passed");
    return 0;
}
