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

}
