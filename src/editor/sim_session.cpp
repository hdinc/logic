#include "sim_session.h"
#include "../ui/file_dialog.h"
#include "../wave/vcd.h"

#include <imgui.h>

#include <fstream>

namespace logic::editor {

sim_session::sim_session(const model::workspace& ws, const model::cell& c, const sim::component_factory& factory)
    : run(std::make_unique<sim::simulator>(ws, c, factory))
    , top(c.id)
{
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
        if (Button(run.running() ? "Pause" : "Run")) run.run(!run.running());
        SameLine(0, 2);
        if (Button("Step")) run.step();
        SameLine(0, 2);
        if (Button("Clear")) run.query([](sim::simulator& s) { s.waveforms.clear(s); });
        SetItemTooltip("Start the history over from now");
        SameLine(0, 2);
        if (Button("Export VCD...")) ui::open_dialog(dialog.c_str(), "Export waveforms", ui::waveform_files, std::filesystem::current_path() / (name + ".vcd"), true);
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
    if (!Begin(label.c_str(), &waveforms_visible)) {
        End();
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
        wave::write_vcd(out, wave_data);
        bool truncated = false;
        for (auto& t : wave_data.traces)
            truncated |= !t.empty() && t.at(0) > wave_data.begin;
        if (!out) wave_message = "Couldn't write " + file.string();
        else
            wave_message = "Exported to " + file.string() +
                (truncated ? ". Only the newest " + std::to_string(sim::waveform_recorder::max_samples) + " changes of each signal are kept." : "");
    }
    End();
}

}
