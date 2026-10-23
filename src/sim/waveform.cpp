#include "waveform.h"
#include "simulator.h"

#include <algorithm>

namespace logic::sim {

waveform_recorder::waveform_recorder()
{
    data.tb = {-12};
}

waveform_recorder::waveform_recorder(waveform_recorder&& other)
{
    std::lock_guard lock(other.mutex);
    data = std::move(other.data);
    dropped = std::move(other.dropped);
    layout = other.layout + 1;
    empty = data.signals.empty();
    other.empty = true;
}

int64_t waveform_recorder::key_of(int net, int lo, int width)
{
    if (width == 0) return net;
    // net in the low 32 bits, lo + 1 and the width above. max_width fits.
    return (int64_t)(uint32_t)net | (int64_t)(lo + 1) << 32 | (int64_t)width << 48;
}

int waveform_recorder::net_of(int64_t key)
{
    return (int)(uint32_t)key;
}

namespace {

// the value of a signal's key
value value_of(const simulator& s, int64_t key)
{
    auto v = s.net_value(waveform_recorder::net_of(key));
    int lo = (int)((key >> 32) & 0xFFFF) - 1;
    if (lo < 0) return v;
    return v.slice(lo, (int)(key >> 48));
}

}

void waveform_recorder::add(const simulator& s, int net, std::string name, int lo, int width)
{
    if (net < 0 || s.net_width(net) == 0) return;
    if (width && (lo < 0 || width < 0 || lo + width > s.net_width(net))) return;
    auto key = key_of(net, lo, width);
    std::lock_guard lock(mutex);
    for (auto& signal : data.signals)
        if (signal.key == key) return;
    if (data.signals.size() >= max_signals) return;
    int w = width ? width : s.net_width(net);
    wave::trace t(wave::kind::digital, w);
    t.push(s.now(), value_of(s, key));
    if (data.signals.empty()) data.begin = s.now();
    data.signals.push_back({key, std::move(name), w, wave::kind::digital, (int)data.traces.size()});
    data.traces.push_back(std::move(t));
    dropped.push_back(0);
    data.end = s.now();
    layout++;
    empty = false;
}

void waveform_recorder::remove(int64_t key)
{
    std::lock_guard lock(mutex);
    for (size_t k = 0; k < data.signals.size(); k++) {
        if (data.signals[k].key != key) continue;
        data.signals.erase(data.signals.begin() + k);
        data.traces.erase(data.traces.begin() + k);
        dropped.erase(dropped.begin() + k);
        for (size_t j = k; j < data.signals.size(); j++)
            data.signals[j].trace = (int)j;
        break;
    }
    layout++;
    empty = data.signals.empty();
}

void waveform_recorder::clear(const simulator& s)
{
    std::lock_guard lock(mutex);
    for (size_t k = 0; k < data.signals.size(); k++) {
        auto& t = data.traces[k];
        t.clear();
        t.push(s.now(), value_of(s, data.signals[k].key));
        dropped[k] = 0;
    }
    data.begin = data.end = s.now();
    data.generation++;
}

void waveform_recorder::update_begin()
{
    data.begin = data.end;
    for (auto& t : data.traces)
        if (!t.empty()) data.begin = std::min(data.begin, t.at(0));
}

void waveform_recorder::capture(const simulator& s)
{
    if (empty.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(mutex);
    data.end = s.now();
    bool truncated = false;
    for (size_t k = 0; k < data.signals.size(); k++) {
        auto& t = data.traces[k];
        // several changes at one time keep the last value, see trace::push
        t.push(s.now(), value_of(s, data.signals[k].key));
        if (t.size() > max_samples) {
            dropped[k] += t.size() - max_samples;
            t.drop_front(t.size() - max_samples);
            truncated = true;
        }
    }
    if (truncated) update_begin();
}

bool waveform_recorder::contains(int net) const
{
    std::lock_guard lock(mutex);
    return std::any_of(data.signals.begin(), data.signals.end(), [net](auto& s) { return net_of(s.key) == net; });
}

size_t waveform_recorder::size() const
{
    std::lock_guard lock(mutex);
    return data.signals.size();
}

wave::trace_set waveform_recorder::snapshot() const
{
    std::lock_guard lock(mutex);
    return data;
}

void waveform_recorder::sync(wave::trace_set& mirror, sync_state& state) const
{
    std::lock_guard lock(mutex);
    if (state.generation != data.generation || state.layout != layout || mirror.traces.size() != data.traces.size()) {
        mirror = data;
        state.generation = data.generation;
        state.layout = layout;
        state.totals.resize(data.traces.size());
        for (size_t k = 0; k < data.traces.size(); k++)
            state.totals[k] = dropped[k] + data.traces[k].size();
        return;
    }

    mirror.begin = data.begin;
    mirror.end = data.end;
    for (size_t k = 0; k < data.traces.size(); k++) {
        auto& src = data.traces[k];
        auto& dst = mirror.traces[k];
        uint64_t total = dropped[k] + src.size();
        uint64_t had = state.totals[k];
        state.totals[k] = total;
        // nothing new: the last change can still have been replaced
        if (total == had && !src.empty() && !dst.empty() && src.at(src.size() - 1) == dst.at(dst.size() - 1) &&
            src.value(src.size() - 1) == dst.value(dst.size() - 1))
            continue;
        // only the last change copied before can have changed since
        uint64_t from = had ? had - 1 : 0;
        if (from < dropped[k]) {
            dst = src;
            continue;
        }
        if (had) dst.pop_back();
        for (size_t i = size_t(from - dropped[k]); i < src.size(); i++)
            dst.push_from(src, i);
        if (dst.size() > src.size()) dst.drop_front(dst.size() - src.size());
    }
}

}
