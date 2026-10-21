#pragma once

#include "format.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace logic::wave {

// how a row draws its signal
enum class display { digital, analog_step, analog_linear };
constexpr const char* display_names[] = {"Digital", "Analog (step)", "Analog (linear)"};

// a row of the view, by signal name so it applies to a regenerated file
struct row_state {
    std::string name;
    std::optional<format> fmt;
    display mode = display::digital;
    // in default row heights
    float height = 1;
    bool expanded = false;
    // IM_COL32 color, 0 for the default
    uint32_t color = 0;
};

struct marker {
    std::string name;
    time t = 0;
};

// what the view shows, saved next to the waveform file
struct session {
    // the waveform file, relative to the session file's folder
    std::string file;
    std::vector<row_state> rows;
    std::optional<time> cursor_a, cursor_b;
    std::vector<marker> markers;
    double view_start = 0, view_span = 0;
    format default_format = format::hex;
    bool full_path = true;
};

std::string session_to_json(const session& s);
// throws std::runtime_error for a file that isn't a session
session session_from_json(std::string_view text);
void save_session(const std::filesystem::path& file, const session& s);
session load_session(const std::filesystem::path& file);

}
