#pragma once

#include "../sim/value.h"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// recorded signals: the simulator's waveforms and vcd files. no gui.
namespace logic::wave {

// in ticks of a timebase
using time = uint64_t;
constexpr time never = std::numeric_limits<time>::max();

// a tick is 10^exponent seconds, the exponent a multiple of 3 from 0 (s)
// to -15 (fs)
struct timebase {
    int exponent = -12;
    bool operator==(const timebase&) const = default;
};

// e.g. "0 s", "12.5 us", "1.000002 s", exact to the tick
std::string format_time(time t, timebase tb);
// a duration in seconds
double seconds(double ticks, timebase tb);
// e.g. "80 kHz", empty for a duration of 0
std::string format_frequency(double ticks, timebase tb);

enum class kind : uint8_t { digital, real, text };

// the changes of one signal in time order. digital values are stored as
// their mask words, see sim::value::data().
class trace {
public:
    static constexpr size_t npos = std::numeric_limits<size_t>::max();

    trace() = default;
    trace(kind k, int width);

    kind type() const { return m_kind; }
    int width() const { return m_width; }
    size_t size() const { return times.size() - head; }
    bool empty() const { return size() == 0; }
    // changes on every modification, unique among all traces: caches made
    // from a trace check it
    uint64_t version() const { return m_version; }

    time at(size_t i) const { return times[head + i]; }
    sim::value value(size_t i) const;
    sim::bit_state bit(size_t i, int b) const;
    bool fully_known(size_t i) const;
    bool fully_floating(size_t i) const;
    double real(size_t i) const { return reals[head + i]; }
    const std::string& text(size_t i) const { return texts[head + i]; }
    // as a number for analog display, NaN if unknown. digital values are
    // read as unsigned or two's complement integers.
    double number(size_t i, bool is_signed) const;
    // what format_value and the tooltips show of change i
    bool same(size_t i, size_t j) const;

    // the last change at or before t, npos if there is none
    size_t index_at(time t) const;
    // the first change after t, npos if there is none
    size_t first_after(time t) const;
    // the first change at or after t, size() if there is none
    size_t lower_bound(time t) const;

    // appends a change. one at the time of the last change replaces it, an
    // unchanged value is dropped. true if the trace got longer.
    bool push(time t, const sim::value& v);
    bool push(time t, double v);
    bool push(time t, std::string v);
    // the same value as change i of other, of the same kind and width
    bool push_from(const trace& other, size_t i);
    void pop_back();
    // removes the oldest n changes, amortized constant time
    void drop_front(size_t n);
    void clear();

    // min and max of number() over changes [a, b), NaN if none is known
    std::pair<double, double> range(size_t a, size_t b, bool is_signed) const;

private:
    void touch();
    void compact();
    const uint64_t* words_of(size_t i) const { return &words[(head + i) * 3 * m_words]; }
    double block_number(size_t physical, bool is_signed) const;

    kind m_kind = kind::digital;
    int m_width = 1;
    int m_words = 1;
    uint64_t m_version = 0;
    // physical index of change 0, the ones before it are dropped
    size_t head = 0;
    std::vector<time> times;
    std::vector<uint64_t> words;
    std::vector<double> reals;
    std::vector<std::string> texts;

    // min/max of blocks of 64 physical changes, built when asked for
    static constexpr size_t block = 64;
    mutable std::vector<std::pair<double, double>> blocks;
    mutable uint64_t blocks_version = 0;
    mutable bool blocks_signed = false;
};

struct signal {
    // stays the same while the signal is there: the net in the simulator,
    // the declaration order in a vcd file
    int64_t key = 0;
    // the full path, scopes separated by '.' or '/'
    std::string name;
    int width = 1;
    kind type = kind::digital;
    // index in trace_set::traces, signals of the same net share a trace
    int trace = 0;

    // after the last scope separator
    std::string_view leaf() const;
    // before it, empty without one
    std::string_view scope() const;
};

struct trace_set {
    timebase tb;
    // the time span of the data: begin is the earliest time that is known
    // about, end the latest
    time begin = 0;
    time end = 0;
    // changes when the history starts over, e.g. the simulation is reset
    uint64_t generation = 0;
    std::vector<signal> signals;
    std::vector<trace> traces;

    const signal* find(int64_t key) const;
    const signal* find_name(std::string_view name) const;
    const trace& trace_of(const signal& s) const { return traces[s.trace]; }
};

// "top/U1.Q" with full_path, "Q" without
std::string display_name(std::string_view name, bool full_path);

}
