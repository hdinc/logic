// the waveform view, drawn by imgui without a native window or gl context.
// catches invalid draw coordinates and widget or frame lifecycle errors.

#include "../src/builtin/builtin.h"
#include "../src/sim/runner.h"
#include "../src/wave/search.h"
#include "../src/wave/ui/view.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdio>

using namespace logic;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("line %d: %s failed\n", __LINE__, #cond); failures++; } } while (0)

static void test_timeline()
{
    wave::timeline timeline;
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

    // stepping while paused moves a following view to the new time
    timeline.follow = true;
    timeline.update(1.0 / 60, 31 * sim::ms, false);
    auto start = timeline.start();
    timeline.update(1.0 / 60, 31 * sim::ms, false);
    CHECK(timeline.start() == start);
    timeline.update(1.0 / 60, 50 * sim::ms, false);
    CHECK(std::abs((50 * sim::ms - timeline.start()) / timeline.span() - 0.95) < 1e-9);

    timeline.zoom_to(300, 100);
    CHECK(timeline.start() == 100 && timeline.span() == 200 && !timeline.follow);
    timeline.reveal(250);
    CHECK(timeline.start() == 100);
    timeline.reveal(1000);
    CHECK(timeline.start() == 900);
}

struct test_host : wave::view_host {
    bool recording = false;
    std::vector<int64_t> removed_keys;

    void removed(int64_t key) override { removed_keys.push_back(key); }
    bool rows_follow_signals() const override { return recording; }
    bool live() const override { return recording; }
};

// draws frames of a view in a window and checks what they draw
struct frames {
    ImGuiIO& io = ImGui::GetIO();
    bool drew = false, filled_bus = false;
    int vertices = 0;

    void operator()(wave::view& view, const wave::trace_set& data, wave::view_host& host, ImVec2 size = {1000, 460})
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({20, 20});
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("waves");
        view.draw(data, host);
        ImGui::End();
        ImGui::Render();
        auto d = ImGui::GetDrawData();
        drew |= d->TotalVtxCount > 0;
        vertices = d->TotalVtxCount;
        for (auto list : d->CmdLists)
            for (auto& vertex : list->VtxBuffer) {
                filled_bus |= vertex.col == ImGui::GetColorU32(ImVec4{80.0f / 255, 200.0f / 255, 130.0f / 255, 0.18f});
                CHECK(std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y));
            }
    }

    ImGuiWindow* plot()
    {
        auto parent = ImGui::FindWindowByName("waves");
        if (!parent) return nullptr;
        for (auto child : parent->DC.ChildWindows)
            if (std::string(child->Name).find("plot") != std::string::npos) return child;
        return nullptr;
    }

    void click(float x, float y, int button = 0)
    {
        io.AddMousePosEvent(x, y);
        io.AddMouseButtonEvent(button, true);
    }
};

// the simulator's recording, mirrored like the waveform window does
static void test_recording(frames& frame)
{
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
    run.query([net](sim::simulator& s) {
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

    wave::trace_set data;
    sim::waveform_recorder::sync_state state;
    wave::view view;
    test_host host;
    host.recording = true;
    auto& io = frame.io;
    auto draw = [&](ImVec2 size = {1000, 460}) {
        run.sim().waveforms.sync(data, state);
        frame(view, data, host, size);
    };

    for (int f = 0; f < 16; f++) {
        // zoom, scrub and pan, then shrink the window and reset the history
        io.AddMousePosEvent(650, 300);
        if (f == 2) io.AddMouseWheelEvent(0, 2);
        if (f == 4) io.AddMouseButtonEvent(0, true);
        if (f == 5) io.AddMouseButtonEvent(0, false);
        if (f == 6) io.AddMouseButtonEvent(2, true);
        if (f == 7) io.AddMousePosEvent(600, 300);
        if (f == 8) io.AddMouseButtonEvent(2, false);
        if (f == 9) {
            io.AddMousePosEvent(70, 300);
            io.AddMouseWheelEvent(0, -3);
        }
        if (f == 12) {
            run.reset();
            run.wait_idle();
        }
        draw(f < 10 ? ImVec2{1000, 460} : ImVec2{240, 300});
    }
    CHECK(view.rows() == 11);
    CHECK(!view.cursor_a); // reset clears the cursors

    // bus expansion and the divider through real mouse events
    draw();
    auto plot = frame.plot();
    CHECK(plot);
    if (!plot) return;
    ImGui::SetScrollY(plot, 0);
    draw();
    float x = plot->Pos.x + plot->WindowPadding.x;
    float y = plot->Pos.y + plot->WindowPadding.y;
    float axis_height = ImGui::GetTextLineHeightWithSpacing() * 2;
    float arrow_center = x + 2 + ImGui::CalcTextSize("x").x + 4 + 2 + (ImGui::GetFontSize() + 2) / 2;
    io.AddMousePosEvent(arrow_center, y + axis_height + 3 + (ImGui::GetFontSize() + 2) / 2);
    draw();
    float scroll_before = plot->ScrollMax.y;
    io.AddMouseButtonEvent(0, true);
    draw();
    io.AddMouseButtonEvent(0, false);
    draw();
    draw();
    draw();
    CHECK(plot->ScrollMax.y > scroll_before + 200); // eight new bit rows

    io.AddMousePosEvent(x + 253, y + 10);
    draw();
    CHECK(ImGui::GetMouseCursor() == ImGuiMouseCursor_ResizeEW);
    io.AddMouseButtonEvent(0, true);
    draw();
    io.AddMousePosEvent(x + 303, y + 10);
    draw();
    bool moved_divider = false;
    for (auto& vertex : plot->DrawList->VtxBuffer)
        moved_divider |= vertex.col == ImGui::GetColorU32(ImGuiCol_SeparatorActive) && std::abs(vertex.pos.x - (x + 300)) < 1;
    CHECK(moved_divider);
    io.AddMouseButtonEvent(0, false);
    draw();

    // the row menu, then removing a row
    io.AddMousePosEvent(x + 100, y + axis_height + 10);
    draw();
    io.AddMouseButtonEvent(1, true);
    draw();
    io.AddMouseButtonEvent(1, false);
    draw();
    CHECK(ImGui::GetCurrentContext()->OpenPopupStack.Size > 0);
    // the first click closes the menu, the second removes the row
    io.AddMousePosEvent(x + 2 + 4, y + axis_height + 3 + 6);
    for (int k = 0; k < 2; k++) {
        io.AddMouseButtonEvent(0, true);
        draw();
        io.AddMouseButtonEvent(0, false);
        draw();
    }
    CHECK(host.removed_keys.size() == 1 && host.removed_keys[0] == net);
    CHECK(view.rows() == 10);
    // the signal is still recorded until the simulation thread removes it,
    // the row doesn't come back meanwhile
    draw();
    CHECK(view.rows() == 10);
}

// a file with a million changes, an analog signal and text
static void test_file(frames& frame)
{
    wave::trace_set data;
    data.tb = {-12};
    auto add = [&](std::string name, wave::kind k, int width) {
        data.signals.push_back({(int64_t)data.signals.size(), name, width, k, (int)data.traces.size()});
        data.traces.emplace_back(k, width);
        return &data.traces.back();
    };
    auto clk = add("top.clk", wave::kind::digital, 1);
    for (int k = 0; k < 1000000; k++)
        clk->push(wave::time(k) * 10, sim::known(k % 2, 1));
    auto count = add("top.count[15:0]", wave::kind::digital, 16);
    for (int k = 0; k < 500000; k++)
        count->push(wave::time(k) * 20 + 5, sim::known(k & 0xFFFF, 16));
    auto level = add("top.level", wave::kind::real, 64);
    for (int k = 0; k < 100000; k++)
        level->push(wave::time(k) * 100, std::sin(k * 0.01));
    auto state = add("top.state", wave::kind::text, 1);
    state->push(0, std::string("IDLE"));
    state->push(3000000, std::string("RUN"));
    data.end = 10000000;

    // rows from a session: modes and formats by name
    wave::session s;
    s.rows = {{"top.clk", std::nullopt, wave::display::digital, 1, false, 0},
        {"top.count[15:0]", wave::format::uint, wave::display::analog_step, 2, false, 0},
        {"top.level", std::nullopt, wave::display::analog_linear, 2, false, 0xFF0000FF},
        {"top.state", std::nullopt, wave::display::digital, 1, false, 0},
        {"top.gone", std::nullopt, wave::display::digital, 1, false, 0}};
    wave::view view;
    auto missing = view.restore(data, s);
    CHECK(missing.size() == 1 && missing[0] == "top.gone");
    CHECK(view.rows() == 4);
    test_host host;
    auto& io = frame.io;
    auto draw = [&] { frame(view, data, host); };

    draw();
    draw();
    // everything in view: drawing doesn't grow with the number of changes
    CHECK(frame.vertices < 100000);
    CHECK(view.timeline.start() == 0 && view.timeline.span() >= 10000000);

    auto plot = frame.plot();
    CHECK(plot);
    if (!plot) return;
    float x = plot->Pos.x + plot->WindowPadding.x;
    float y = plot->Pos.y + plot->WindowPadding.y;
    float axis_height = ImGui::GetTextLineHeightWithSpacing() * 2;
    float left = x + 250 + 6;
    float clk_y = y + axis_height + 15;

    // clicking on the clock snaps cursor A to one of its transitions
    frame.click(left + 300, clk_y);
    draw();
    io.AddMouseButtonEvent(0, false);
    draw();
    CHECK(view.cursor_a && *view.cursor_a % 10 == 0 && *view.cursor_a > 0);
    // shift+click sets cursor B
    io.AddKeyEvent(ImGuiMod_Shift, true);
    frame.click(left + 500, clk_y);
    draw();
    io.AddMouseButtonEvent(0, false);
    io.AddKeyEvent(ImGuiMod_Shift, false);
    draw();
    CHECK(view.cursor_b && *view.cursor_b > *view.cursor_a);

    // right arrow: the next transition of any row
    auto a = *view.cursor_a;
    io.AddKeyEvent(ImGuiKey_RightArrow, true);
    draw();
    io.AddKeyEvent(ImGuiKey_RightArrow, false);
    draw();
    CHECK(view.cursor_a && *view.cursor_a > a && *view.cursor_a <= a + 10);
    io.AddKeyEvent(ImGuiKey_LeftArrow, true);
    draw();
    io.AddKeyEvent(ImGuiKey_LeftArrow, false);
    draw();
    CHECK(view.cursor_a && *view.cursor_a == a);

    // a marker at cursor A
    io.AddKeyEvent(ImGuiKey_M, true);
    draw();
    io.AddKeyEvent(ImGuiKey_M, false);
    draw();
    CHECK(view.markers.size() == 1 && view.markers[0].t == a);

    // dragging over the ruler zooms to that range
    double span = view.timeline.span();
    frame.click(left + 100, y + 5);
    draw();
    io.AddMousePosEvent(left + 200, y + 5);
    draw();
    io.AddMouseButtonEvent(0, false);
    draw();
    CHECK(view.timeline.span() < span / 5);
    draw();
    CHECK(frame.vertices < 100000);

    // the overview moves the view
    double start = view.timeline.start();
    auto parent = ImGui::FindWindowByName("waves");
    float bar_y = parent->Pos.y + parent->Size.y - parent->WindowPadding.y - 6;
    frame.click(parent->Pos.x + parent->Size.x * 0.8f, bar_y);
    draw();
    io.AddMouseButtonEvent(0, false);
    draw();
    CHECK(view.timeline.start() > start);

    // the session keeps it all
    auto saved = view.save(data);
    CHECK(saved.rows.size() == 4 && saved.rows[1].mode == wave::display::analog_step && saved.rows[1].fmt == wave::format::uint);
    CHECK(saved.rows[2].color == 0xFF0000FF);
    CHECK(saved.markers.size() == 1 && saved.cursor_a == view.cursor_a && saved.cursor_b == view.cursor_b);
    wave::view other;
    CHECK(other.restore(data, saved).empty());
    CHECK(other.rows() == 4 && other.timeline.start() == view.timeline.start());
}

// a bundle recorded once with its fields, and one field on its own
static void test_bundle_rows(frames& frame)
{
    model::workspace ws;
    ws.add_builtin_library(builtin::registry());
    auto& cell = ws.create_cell(ws.add_library("work"), "top");
    cell.add_port(model::direction::input, {-20, 0}).width = 12;
    cell.add_port(model::direction::output, {20, 0}).width = 12;
    sim::runner run(std::make_unique<sim::simulator>(ws, cell, builtin::factory()));
    int net = run.sim().net_of_port(run.sim().root(), 0);
    std::vector<wave::field> fields{
        {"valid", 0, 1, 0, false},
        {"data", 1, 8, 0, false},
        {"s", 9, 3, 0, true},
        {"s.x", 9, 1, 1, false},
        {"s.y", 10, 2, 1, false},
    };
    auto data_key = sim::waveform_recorder::key_of(net, 1, 8);
    run.query([&](sim::simulator& s) {
        s.waveforms.add(s, net, "top/bus", 0, 0, fields);
        s.waveforms.add(s, net, "top/bus.data", 1, 8);
        s.set_input(0, sim::known(0xAB << 1 | 1, 12));
        s.run_until(sim::ms);
        s.set_input(0, sim::known(0xCD << 1 | 0b101 << 9, 12));
        s.run_until(2 * sim::ms);
    });
    run.wait_idle();
    auto& rec = run.sim().waveforms;
    CHECK(rec.size() == 2 && rec.has(net) && rec.has(data_key) && !rec.has(sim::waveform_recorder::key_of(net, 0, 1)));

    wave::trace_set data;
    sim::waveform_recorder::sync_state state;
    rec.sync(data, state);
    auto bus = data.find(net);
    CHECK(bus && bus->fields == fields && bus->width == 12);
    auto alone = data.find(data_key);
    CHECK(alone && alone->fields.empty() && alone->width == 8 && data.trace_of(*alone).value(data.trace_of(*alone).size() - 1) == sim::known(0xCD, 8));

    wave::view view;
    test_host host;
    host.recording = true;
    auto draw = [&] {
        rec.sync(data, state);
        frame(view, data, host, {1000, 300});
    };
    for (int f = 0; f < 3; f++)
        draw();
    CHECK(view.rows() == 2);
    auto plot = frame.plot();
    CHECK(plot);
    if (!plot) return;
    float collapsed = plot->ContentSize.y;

    // expanded: a line per field, closed groups hide theirs
    auto session = view.save(data);
    for (auto& r : session.rows)
        r.expanded = r.name == "top/bus";
    view.restore(data, session);
    for (int f = 0; f < 3; f++)
        draw();
    float expanded = plot->ContentSize.y;
    float base = std::max(40.0f, ImGui::GetTextLineHeightWithSpacing() * 2);
    CHECK(expanded > collapsed + 4 * base * 0.8f * 0.9f);

    // the data field opens into its 8 bits with its caret: lines are the
    // row, valid, then data
    auto& io = frame.io;
    ImGui::SetScrollY(plot, 0);
    draw();
    float x = plot->Pos.x + plot->WindowPadding.x;
    float y = plot->Pos.y + plot->WindowPadding.y + ImGui::GetTextLineHeightWithSpacing() * 2;
    float ph = base * 0.8f;
    float caret_x = x + ImGui::GetFontSize() * 3 + (ImGui::GetFontSize() + 4) / 2;
    float data_y = y + base + ph + ph / 2;
    io.AddMousePosEvent(caret_x, data_y);
    draw();
    io.AddMouseButtonEvent(0, true);
    draw();
    io.AddMouseButtonEvent(0, false);
    // the content size follows a frame later
    for (int f = 0; f < 3; f++)
        draw();
    float with_bits = plot->ContentSize.y;
    CHECK(std::abs(with_bits - (expanded + 8 * ph)) < 2);

    // dragging the row's lower border makes it taller, the lines under it
    // stay
    io.AddMousePosEvent(x + 150, y + base);
    draw();
    io.AddMouseButtonEvent(0, true);
    draw();
    io.AddMousePosEvent(x + 150, y + base + 30);
    draw();
    io.AddMousePosEvent(x + 150, y + base + 60);
    draw();
    io.AddMouseButtonEvent(0, false);
    for (int f = 0; f < 3; f++)
        draw();
    CHECK(std::abs(plot->ContentSize.y - (with_bits + 60)) < 2);
    CHECK(!view.cursor_a);
}

int main()
{
    test_timeline();

    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1280, 720};
    io.DeltaTime = 1.0f / 60;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    frames frame;
    test_recording(frame);
    test_file(frame);
    test_bundle_rows(frame);
    CHECK(frame.drew && frame.filled_bus);
    ImGui::DestroyContext();

    if (failures) return 1;
    std::puts("waveform view frames passed");
    return 0;
}
