#include "search.h"

namespace logic::wave {

namespace {

// change i is a change of the bit, or of the whole value for bit -1
bool changes(const trace& t, size_t i, int bit)
{
    if (i == 0) return false;
    if (bit < 0) return true;
    return t.bit(i, bit) != t.bit(i - 1, bit);
}

}

time next_change(const trace& t, time after, int bit)
{
    size_t i = t.first_after(after);
    if (i == trace::npos) return never;
    for (; i < t.size(); i++)
        if (changes(t, i, bit)) return t.at(i);
    return never;
}

time prev_change(const trace& t, time before, int bit)
{
    if (before == 0) return never;
    size_t j = t.index_at(before - 1);
    if (j == trace::npos) return never;
    for (; j > 0; j--)
        if (changes(t, j, bit)) return t.at(j);
    return never;
}

time next_match(const trace& t, time after, const pattern& p)
{
    size_t i = t.first_after(after);
    if (i == trace::npos) return never;
    for (; i < t.size(); i++)
        if (p.matches(t, i) && (i == 0 || !p.matches(t, i - 1))) return t.at(i);
    return never;
}

time prev_match(const trace& t, time before, const pattern& p)
{
    if (before == 0) return never;
    size_t j = t.index_at(before - 1);
    if (j == trace::npos) return never;
    for (;; j--) {
        if (p.matches(t, j) && (j == 0 || !p.matches(t, j - 1))) return t.at(j);
        if (j == 0) break;
    }
    return never;
}

trace slice_trace(const trace& t, int lo, int width)
{
    trace result(kind::digital, width);
    for (size_t i = 0; i < t.size(); i++)
        result.push(t.at(i), t.value(i).slice(lo, width));
    return result;
}

size_t group_end(const std::vector<field>& fields, size_t group)
{
    size_t k = group + 1;
    while (k < fields.size() && fields[k].depth > fields[group].depth)
        k++;
    return k;
}

trace_set expand_fields(const trace_set& set)
{
    trace_set r;
    r.tb = set.tb;
    r.begin = set.begin;
    r.end = set.end;
    r.generation = set.generation;
    for (auto& s : set.signals) {
        if (s.fields.empty()) {
            auto copy = s;
            copy.trace = (int)r.traces.size();
            r.traces.push_back(set.trace_of(s));
            r.signals.push_back(std::move(copy));
            continue;
        }
        auto& t = set.trace_of(s);
        for (size_t k = 0; k < s.fields.size(); k++) {
            auto& f = s.fields[k];
            if (f.group) continue;
            signal leaf;
            // a key per field that no other signal has
            leaf.key = s.key ^ (int64_t(k + 1) << 56);
            leaf.name = s.name + "." + f.name;
            leaf.width = f.width;
            leaf.type = kind::digital;
            leaf.trace = (int)r.traces.size();
            r.traces.push_back(slice_trace(t, f.lo, f.width));
            r.signals.push_back(std::move(leaf));
        }
    }
    return r;
}

trace bit_trace(const trace& t, int bit)
{
    trace result(kind::digital, 1);
    for (size_t i = 0; i < t.size(); i++) {
        sim::value v(1, 0);
        v.set(0, t.bit(i, bit));
        result.push(t.at(i), v);
    }
    return result;
}

}
