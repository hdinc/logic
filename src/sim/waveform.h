#pragma once

#include "component.h"

#include <atomic>
#include <deque>
#include <mutex>

namespace logic::sim {

class simulator;

struct waveform_sample {
    time at;
    value v;
};

struct waveform_signal {
    int net;
    std::string name;
    std::deque<waveform_sample> samples;
    time first = 0;
    bool truncated = false;
};

struct waveform_data {
    std::vector<waveform_signal> signals;
    time now = 0;
    uint64_t generation = 0;
};

// Written on the simulation thread, read by the GUI. Records settled values
// at every event, independently of runner's throttled display snapshots.
class waveform_recorder {
public:
    waveform_recorder() = default;
    waveform_recorder(waveform_recorder&& other);

    static constexpr size_t max_signals = 64;
    static constexpr size_t max_samples = 10000; // per signal, retain newest

    // Mutations are called on the simulation thread (through runner::post).
    void add(const simulator& s, int net, std::string name);
    void remove(int net);
    void clear(const simulator& s);
    void capture(const simulator& s);

    // Includes the value preceding from, to draw segments crossing the edge.
    waveform_data read(time from = 0, time to = never) const;

private:
    mutable std::mutex mutex;
    std::atomic<bool> empty{true};
    waveform_data data;
};

}
