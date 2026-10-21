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
