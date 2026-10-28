#include "sim_session.h"
#include "../ui/file_dialog.h"
#include "../ui/icons.h"
#include "../ui/zoom.h"
#include "../wave/search.h"
#include "../wave/vcd.h"

#include <imgui.h>

#include <fstream>

namespace logic::editor {

sim_session::sim_session(const model::workspace& ws, const model::cell& c, const sim::component_factory& factory)
    : run(std::make_unique<sim::simulator>(ws, c, factory))
    , top(c.id)
{
    for (auto& d : run.sim().displays()) {
        display_window w;
        w.d = &d;
        displays.push_back(std::move(w));
    }
}

bool sim_session::has_display(int component) const
{
    return std::any_of(displays.begin(), displays.end(), [&](auto& w) { return w.d->component == component; });
}

bool sim_session::show_display(int component)
{
    for (auto& w : displays) {
        if (w.d->component != component) continue;
        w.open = true;
        ImGui::SetWindowFocus(("###display" + std::to_string(top) + "_" + std::to_string(component)).c_str());
        return true;
    }
    return false;
}

void sim_session::displays_gui()
{
    for (auto& w : displays)
        if (w.open) display_gui(w);
}

void sim_session::display_gui(display_window& w)
{
    using namespace ImGui;

    auto& buf = *w.d->pixels;
    auto label = "Framebuffer: " + w.d->path + "###display" + std::to_string(top) + "_" + std::to_string(w.d->component);
    SetNextWindowSize({std::clamp(buf.width * 4.0f, 240.0f, 800.0f) + 20, std::clamp(buf.height * 4.0f, 180.0f, 600.0f) + 60}, ImGuiCond_FirstUseEver);
    SetNextWindowPos(GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    if (!ui::zoom::begin(label.c_str(), &w.open, ImGuiWindowFlags_NoCollapse)) {
        ui::zoom::end();
        return;
    }

    if (!w.tex) {
        w.tex.emplace(Hd::Texture::emptyTexture(buf.width, buf.height));
        // for backends without samplers, see the callbacks around Image
        glBindTexture(GL_TEXTURE_2D, w.tex->Id());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    bool changed = false;
    {
        std::lock_guard lk(buf.m);
        if (w.version != buf.version) {
            w.pixels = buf.rgba;
            w.version = buf.version;
            changed = true;
        }
    }
    if (changed) {
        glBindTexture(GL_TEXTURE_2D, w.tex->Id());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, buf.width, buf.height, GL_RGBA, GL_UNSIGNED_BYTE, w.pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    static const char* const zooms[] = {"Fit", "1x", "2x", "4x", "8x"};
    static const int factors[] = {0, 1, 2, 4, 8};
    int pick = 0;
    for (int k = 0; k < 5; k++)
        if (factors[k] == w.zoom) pick = k;
    SetNextItemWidth(GetFontSize() * 5);
    if (Combo("Zoom", &pick, zooms, 5)) w.zoom = factors[pick];
    SameLine();
    TextDisabled("%d x %d", buf.width, buf.height);

    BeginChild("picture", {0, 0}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    auto avail = GetContentRegionAvail();
    float scale = (float)w.zoom;
    // as large as fits, keeping the aspect ratio
    if (w.zoom == 0) scale = std::max(std::min(avail.x / buf.width, avail.y / buf.height), 0.01f);
    ImVec2 size{buf.width * scale, buf.height * scale};
    // centered while it fits
    auto at = GetCursorPos();
    SetCursorPos({at.x + std::max(0.0f, (avail.x - size.x) / 2), at.y + std::max(0.0f, (avail.y - size.y) / 2)});
    auto origin = GetCursorScreenPos();
    // imgui samples linearly, which would blur the pixels
    auto& pio = GetPlatformIO();
    if (pio.DrawCallback_SetSamplerNearest) GetWindowDrawList()->AddCallback(pio.DrawCallback_SetSamplerNearest, nullptr);
    Image((ImTextureID)w.tex->Id(), size);
    if (pio.DrawCallback_SetSamplerLinear) GetWindowDrawList()->AddCallback(pio.DrawCallback_SetSamplerLinear, nullptr);
    if (IsItemHovered() && !w.pixels.empty()) {
        auto m = GetMousePos();
        int x = std::clamp(int((m.x - origin.x) / scale), 0, buf.width - 1);
        int y = std::clamp(int((m.y - origin.y) / scale), 0, buf.height - 1);
        uint32_t c = w.pixels[size_t(y) * buf.width + x];
        SetTooltip("x %d, y %d: #%02X%02X%02X", x, y, c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
    }
    EndChild();
    ui::zoom::end();
}

void sim_session::watch(const sim::scope* s, int instance)
{
    watch_scope = s;
    watch_instance = instance;
    std::lock_guard lk(watch_state->m);
    watch_state->data = {};
}

void sim_session::refresh_watch(int memory, uint64_t address, size_t bytes, std::optional<uint64_t> from, int lines)
{
    if (!watch_scope || watch_instance < 0) return;
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard lk(watch_state->m);
        // one request at a time, not too often while running
        if (watch_state->pending) return;
        if (run.running() && now - last_refresh < std::chrono::milliseconds(100)) return;
        watch_state->pending = true;
    }
    last_refresh = now;

    run.query([state = watch_state, s = watch_scope, k = watch_instance, memory, address, bytes, from, lines](sim::simulator& sim) {
        watch_data d;
        d.memory = memory;
        d.memory_address = address;
        if (auto c = sim.component_of(*s, k)) {
            d.valid = true;
            c->inspect(d.items);
            if (auto dbg = c->debug()) {
                d.debug = true;
                d.memories = dbg->memories();
                d.pc = dbg->pc();
                d.breakpoints = dbg->breakpoints();
                if (memory >= 0 && memory < (int)d.memories.size() && address < d.memories[memory].size) {
                    d.bytes.resize(std::min<uint64_t>(bytes, d.memories[memory].size - address));
                    if (!dbg->read_memory(memory, address, d.bytes)) d.bytes.clear();
                }
                auto at = from ? from : d.pc;
                for (int n = 0; at && n < lines; n++) {
                    std::string text;
                    int len = dbg->disassemble(*at, text);
                    if (len <= 0) break;
                    d.disassembly.push_back({*at, len, text});
                    *at += len;
                }
            }
        }
        std::lock_guard lk(state->m);
        state->data = std::move(d);
        state->pending = false;
    });
}

watch_data sim_session::watched() const
{
    std::lock_guard lk(watch_state->m);
    return watch_state->data;
}

void sim_session::debug(std::function<void(sim::debug_interface&)> f)
{
    if (!watch_scope || watch_instance < 0) return;
    run.post([s = watch_scope, k = watch_instance, f = std::move(f)](sim::simulator& sim) {
        if (auto c = sim.component_of(*s, k))
            if (auto d = c->debug()) f(*d);
    });
}

void sim_session::interact(const sim::scope* s, int instance, std::string action, int64_t value)
{
    run.post([s, instance, action = std::move(action), value](sim::simulator& sim) { sim.interact(*s, instance, action, value); });
}

bool sim_session::uses(model::id cell) const
{
    auto& c = run.sim().cells();
    return std::find(c.begin(), c.end(), cell) != c.end();
}


namespace {

// run controls and export in the view's toolbar, rows for every recorded
// signal
class recording_host : public wave::view_host {
public:
    recording_host(sim::runner& run, std::string dialog, std::string name)
        : run(run)
        , dialog(std::move(dialog))
        , name(std::move(name))
    {
    }

    void toolbar() override
    {
        using namespace ImGui;
        if (ui::small_icon_button(run.running() ? ui::icon::pause : ui::icon::play, run.running() ? "Pause" : "Run")) run.run(!run.running());
        SameLine(0, 2);
        if (ui::small_icon_button(ui::icon::step, "Step")) run.step();
        SameLine(0, 2);
        if (ui::small_icon_button(ui::icon::eraser, "Clear: start the history over from now")) run.query([](sim::simulator& s) { s.waveforms.clear(s); });
        SameLine(0, 2);
        if (ui::small_icon_button(ui::icon::download, "Export VCD..."))
            ui::open_dialog(dialog.c_str(), "Export waveforms", ui::waveform_files, std::filesystem::current_path() / (name + ".vcd"), true);
        SameLine(0, 16);
    }
    void removed(int64_t key) override
    {
        run.query([key](sim::simulator& s) { s.waveforms.remove(key); });
    }
    bool rows_follow_signals() const override { return true; }
    bool live() const override { return true; }
    bool running() const override { return run.running(); }

private:
    sim::runner& run;
    std::string dialog;
    std::string name;
};

}

void sim_session::waveforms_gui(const std::string& title)
{
    using namespace ImGui;
    if (!waveforms_visible) return;
    run.sim().waveforms.sync(wave_data, wave_sync);

    SetNextWindowSize({1000, 460}, ImGuiCond_FirstUseEver);
    auto id = std::to_string(top);
    auto label = "Waveforms: " + title + "###waveforms" + id;
    // double-clicking the title zooms it instead of collapsing
    if (!ui::zoom::begin(label.c_str(), &waveforms_visible, ImGuiWindowFlags_NoCollapse)) {
        ui::zoom::end();
        return;
    }
    auto dialog = "export_vcd_" + id;
    recording_host host(run, dialog, title);
    if (!wave_message.empty()) {
        TextDisabled("%s", wave_message.c_str());
        SameLine();
        if (SmallButton("OK")) wave_message.clear();
    }
    waves.draw(wave_data, host);

    std::filesystem::path file;
    if (ui::dialog_result(dialog.c_str(), file)) {
        std::ofstream out(file);
        // a bundle's fields as signals of their own, in a scope
        wave::write_vcd(out, wave::expand_fields(wave_data));
        bool truncated = false;
        for (auto& t : wave_data.traces)
            truncated |= !t.empty() && t.at(0) > wave_data.begin;
        if (!out) wave_message = "Couldn't write " + file.string();
        else
            wave_message = "Exported to " + file.string() +
                (truncated ? ". Only the newest " + std::to_string(sim::waveform_recorder::max_samples) + " changes of each signal are kept." : "");
    }
    ui::zoom::end();
}

}
