#include "waveform.h"
#include "simulator.h"

#include <algorithm>

namespace logic::sim {

waveform_recorder::waveform_recorder(waveform_recorder&& other)
{
    std::lock_guard lock(other.mutex);
    data = std::move(other.data);
    empty = data.signals.empty();
    other.empty = true;
}

void waveform_recorder::add(const simulator& s, int net, std::string name)
{
    if (net < 0) return;
    std::lock_guard lock(mutex);
    for (auto& signal : data.signals)
        if (signal.net == net) return;
    if (data.signals.size() >= max_signals) return;
    data.signals.push_back({net, std::move(name), {{s.now(), s.net_value(net)}}, s.now()});
    data.now = s.now();
    empty = false;
}

void waveform_recorder::remove(int net)
{
    std::lock_guard lock(mutex);
    std::erase_if(data.signals, [net](auto& signal) { return signal.net == net; });
    empty = data.signals.empty();
}

void waveform_recorder::clear(const simulator& s)
{
    std::lock_guard lock(mutex);
    for (auto& signal : data.signals) {
        signal.samples = {{s.now(), s.net_value(signal.net)}};
        signal.first = s.now();
        signal.truncated = false;
    }
    data.now = s.now();
    data.generation++;
}

void waveform_recorder::capture(const simulator& s)
{
    if (empty.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(mutex);
    data.now = s.now();
    for (auto& signal : data.signals) {
        auto v = s.net_value(signal.net);
        auto& samples = signal.samples;
        if (samples.back().v == v) continue;
        // Several UI actions can change a net at the same simulated time.
        // Keep its final value, rather than zero-width display segments.
        if (samples.back().at == s.now()) samples.back().v = std::move(v);
        else samples.push_back({s.now(), std::move(v)});
        if (samples.size() > max_samples) {
            samples.pop_front();
            signal.truncated = true;
            signal.first = samples.front().at;
        }
    }
}

waveform_data waveform_recorder::read(time from, time to) const
{
    std::lock_guard lock(mutex);
    waveform_data result;
    result.now = data.now;
    result.generation = data.generation;
    for (auto& signal : data.signals) {
        auto begin = std::lower_bound(signal.samples.begin(), signal.samples.end(), from,
            [](auto& sample, time t) { return sample.at < t; });
        if (begin != signal.samples.begin()) --begin;
        auto end = std::upper_bound(begin, signal.samples.end(), to,
            [](time t, auto& sample) { return t < sample.at; });
        result.signals.push_back({signal.net, signal.name, {begin, end}, signal.first, signal.truncated});
    }
    return result;
}

}
