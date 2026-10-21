#pragma once

#include "../sim/runner.h"

#include <map>
#include <optional>
#include <string_view>

namespace logic::editor {

enum class waveform_format { hex, uint, sint, binary, octal };

std::string format_waveform_value(const sim::value& v, waveform_format format);
std::string waveform_signal_name(std::string_view name, bool full_path);

// Time navigation is independent of rendering, so zoom animation also works
// while the simulator is paused. Follow keeps up with running and stepping,
// only manual panning turns it off.
class waveform_timeline {
public:
    void zoom(double wheel, double fraction);
    void pan(double fraction);
    void fit(sim::time first, sim::time last);
    void update(double dt, sim::time now, bool running);
    double start() const { return m_start; }
    double span() const { return m_span; }
    bool follow = true;

private:
    double m_start = 0;
    double m_span = double(sim::seconds);
    double target_span = m_span;
    double anchor = 0;
    double anchor_fraction = 0;
    // time at the last update, never before the first one
    sim::time last_now = sim::never;
};

// One timeline per simulation session, shared by all its hierarchy views.
class waveform_view {
public:
    void gui(sim::runner& run, const std::string& title, model::id id);
    bool visible = false;

private:
    waveform_timeline timeline;
    float signal_width = 250;
    bool full_path = true;
    waveform_format default_format = waveform_format::hex;
    struct signal_options {
        bool expanded = false;
        std::optional<waveform_format> format;
    };
    std::map<int, signal_options> options;
    bool fit = true;
    bool has_cursor = false;
    sim::time cursor = 0;
    uint64_t generation = 0;
};

}
