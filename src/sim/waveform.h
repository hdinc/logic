#pragma once

#include "../wave/trace.h"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace logic::sim {

class simulator;

// Written on the simulation thread, read by the GUI. Records settled values
// at every point in time, independently of runner's throttled display
// snapshots. Signal keys are nets, times picoseconds.
class waveform_recorder {
public:
    waveform_recorder();
    waveform_recorder(waveform_recorder&& other);

    static constexpr size_t max_signals = 64;
    static constexpr size_t max_samples = 10000; // per signal, retain newest

    // Mutations are called on the simulation thread (through runner::query).
    void add(const simulator& s, int net, std::string name);
    void remove(int net);
    // starts the history over from the current values
    void clear(const simulator& s);
    void capture(const simulator& s);

    bool contains(int net) const;
    size_t size() const;
    // a copy of everything recorded
    wave::trace_set snapshot() const;

    // what a mirror has of the recording
    struct sync_state {
        uint64_t generation = ~uint64_t(0);
        uint64_t layout = ~uint64_t(0);
        std::vector<uint64_t> totals;
    };
    // brings mirror up to date with the recording. only what changed since
    // the last sync with the same state is copied.
    void sync(wave::trace_set& mirror, sync_state& state) const;

private:
    void update_begin();

    mutable std::mutex mutex;
    std::atomic<bool> empty{true};
    // one trace per signal
    wave::trace_set data;
    // changes dropped from the front of each trace
    std::vector<uint64_t> dropped;
    // bumped when signals are added or removed
    uint64_t layout = 0;
};

}
