#pragma once

#include "trace.h"

#include <atomic>
#include <filesystem>
#include <ostream>
#include <string_view>

namespace logic::wave {

// shared with a thread loading a file
struct load_progress {
    std::atomic<float> fraction{0};
    // set to stop loading, load_vcd then throws
    std::atomic<bool> cancel{false};
};

// value change dump files (IEEE 1364), with the std_logic states of vhdl
// tools. signal names are the scopes and the reference joined by '.', keys
// the order of the declarations. throws std::runtime_error with the line of
// a problem.
trace_set load_vcd(const std::filesystem::path& file, load_progress* progress = nullptr);
trace_set parse_vcd(std::string_view text, load_progress* progress = nullptr);

// writes the signals, scopes from their names. signals of one trace share
// an identifier.
void write_vcd(std::ostream& out, const trace_set& set);

}
