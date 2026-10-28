#pragma once

#include "format.h"

namespace logic::wave {

// a change is a time at which the value differs from the one before. bit
// -1 is the whole value, otherwise the changes of that bit. never if there
// is none.
time next_change(const trace& t, time after, int bit = -1);
time prev_change(const trace& t, time before, int bit = -1);

// the next time after `after` (or the last before `before`) at which the
// value starts to match p
time next_match(const trace& t, time after, const pattern& p);
time prev_match(const trace& t, time before, const pattern& p);

// the changes of one bit of a digital trace, as a trace of width 1
trace bit_trace(const trace& t, int bit);
// the changes of bits [lo, lo + width) of a digital trace
trace slice_trace(const trace& t, int lo, int width);

// the end of the fields of the group at index `group`: the first field
// after it that isn't deeper. fields.size() for the last.
size_t group_end(const std::vector<field>& fields, size_t group);

// the signals with fields replaced by a signal per field that isn't a
// group, named name.field with the bits as their own trace. e.g. for a vcd
// file, where they become a scope.
trace_set expand_fields(const trace_set& set);

}
