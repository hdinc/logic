#pragma once

#include "../sim/runner.h"
#include "../wave/ui/view.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <optional>

namespace logic::editor {

// a running simulation of a top cell, shared by the views showing it. the
// cell editor of the top cell owns it, views of instances in other tabs
// close when it is stopped. the simulation runs on its own thread, views
// draw the values of the last update().
// what the debugging view shows of one instance, read on the simulation
// thread
struct watch_data {
    // the instance has a component
    bool valid = false;
    std::vector<sim::inspect_item> items;

    // the rest only if it has something to debug
    bool debug = false;
    std::vector<sim::memory_space> memories;
    std::optional<uint64_t> pc;
    struct line {
        uint64_t address;
        int length;
        std::string text;
    };
    std::vector<line> disassembly;
    std::vector<uint64_t> breakpoints;
    // the part of a memory asked for
    int memory = 0;
    uint64_t memory_address = 0;
    std::vector<uint8_t> bytes;
};

class sim_session {
public:
    // the cells of the hierarchy must not change while it runs
    sim_session(const model::workspace& ws, const model::cell& top, const sim::component_factory& factory);

    // takes the latest values from the simulation thread, once per frame
    void update() { run.sync(); }

    // the instance debugging views show, -1 for none. the scope stays valid
    // as long as the session.
    void watch(const sim::scope* s, int instance);
    const sim::scope* watched_scope() const { return watch_scope; }
    int watched_instance() const { return watch_instance; }
    // asks for fresh data of the watched instance: bytes of a memory and the
    // disassembly from an address (or the program counter). every frame while
    // paused, a few times a second while running.
    void refresh_watch(int memory, uint64_t address, size_t bytes, std::optional<uint64_t> disassemble_from, int lines);
    watch_data watched() const;
    // runs f with the watched instance's debug interface on the simulation
    // thread
    void debug(std::function<void(sim::debug_interface&)> f);

    // a user action on an instance, see sim::component::interact
    void interact(const sim::scope* s, int instance, std::string action, int64_t value);

    bool uses(model::id cell) const;

    // the values to draw
    const sim::snapshot& sim() const { return run.view(); }

    // the waveform window of the recorded signals, see waveform_recorder
    void waveforms_gui(const std::string& title);

    sim::runner run;
    model::id top;
    bool waveforms_visible = false;

    // set when any view stops the simulation, the owner then drops it
    bool stopped = false;

private:
    // the recording as of this frame, updated by waveforms_gui
    wave::trace_set wave_data;
    sim::waveform_recorder::sync_state wave_sync;
    wave::view waves;
    std::string wave_message;

    const sim::scope* watch_scope = nullptr;
    int watch_instance = -1;
    // shared with the simulation thread
    struct shared {
        std::mutex m;
        watch_data data;
        bool pending = false;
    };
    std::shared_ptr<shared> watch_state = std::make_shared<shared>();
    std::chrono::steady_clock::time_point last_refresh;
};

}
