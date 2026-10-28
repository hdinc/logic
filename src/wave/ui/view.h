#pragma once

#include "../session.h"
#include "timeline.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace logic::wave {

// what the app around a view provides
class view_host {
public:
    virtual ~view_host() = default;
    // the first items of the toolbar, e.g. run controls
    virtual void toolbar() {}
    // the user removed the row of a signal
    virtual void removed(int64_t key) { (void)key; }
    // every signal of the trace set gets a row. otherwise rows are added
    // with view::add or by dropping signals on the view.
    virtual bool rows_follow_signals() const { return false; }
    // the data grows while running, follow keeps its end in view
    virtual bool live() const { return false; }
    virtual bool running() const { return false; }
};

// rows of signals over time, with cursors, markers, search and an overview
// of the whole time range. draws into the current window, the data can
// change between frames.
class view {
public:
    // drag and drop payload with a signal key (int64_t), dropping it on
    // the view adds a row
    static constexpr const char* signal_payload = "WAVE_SIGNAL";

    void draw(const trace_set& data, view_host& host);

    // a row for the signal, at the end or before row `at`
    void add(const trace_set& data, int64_t key, int at = -1);
    bool has(int64_t key) const;
    size_t rows() const { return m_rows.size(); }
    // no rows, cursors or markers
    void clear();
    // fits the whole time range on the next draw
    void fit_all() { fit = true; }

    // the view with its rows by name, and back. restore returns the names
    // without a signal in data.
    session save(const trace_set& data) const;
    std::vector<std::string> restore(const trace_set& data, const session& s);

    std::optional<time> cursor_a, cursor_b;
    std::vector<marker> markers;
    wave::timeline timeline;
    format default_format = format::hex;
    bool full_path = true;

private:
    struct row {
        int64_t key = 0;
        std::optional<format> fmt;
        display mode = display::digital;
        float height = 1;
        bool expanded = false;
        uint32_t color = 0;
        // analog values scaled to the visible part instead of everything
        bool fit_visible = false;
        // the groups among the signal's fields that are closed, by index
        std::set<int> closed;
    };
    // a drawn line: a row, or one bit of an expanded row, or a field of a
    // row whose signal has fields
    struct line {
        int row;
        // the bit or the field, -1 for the row itself
        int bit;
        float top, height;
        // of a field
        int depth = 0;
    };
    using line_id = std::pair<int64_t, int>;

    void sync_rows(const trace_set& data, view_host& host);
    void toolbar(const trace_set& data, view_host& host);
    void status_line(const trace_set& data);
    void plot(const trace_set& data, view_host& host, float height);
    void overview(const trace_set& data, float height);
    void shortcuts(const trace_set& data);
    void row_menu(const trace_set& data, row& r, const signal& s);

    // the trace a line shows: the row's, or the bit's made from it
    const trace* line_trace(const trace_set& data, const line_id& id);
    bool selected(const line_id& id) const;
    // the selected lines, or every row's if none is
    std::vector<line_id> targets() const;
    void step(const trace_set& data, bool forward, bool b);
    void find(const trace_set& data, bool forward);
    void jump_marker(bool forward);
    void set_cursor(bool b, time t);
    void remove_row(int index, view_host& host);
    std::pair<time, time> data_range(const trace_set& data) const;

    std::vector<row> m_rows;
    std::vector<line_id> selection;
    // removed rows whose signal is still in the data, see sync_rows
    std::vector<int64_t> removed;
    // bit and field traces of expanded rows and the summaries of bundles,
    // remade when their trace changes
    std::map<line_id, std::pair<uint64_t, trace>> parts;

    bool fit = true;
    bool started = false;
    uint64_t generation = ~uint64_t(0);
    float name_width = 250;
    // value search
    std::string search_text;
    std::string search_status;
    // dragging over the ruler to zoom, from this time
    std::optional<double> region_from;
    // dragging the overview's window, this far into it
    std::optional<double> overview_grab;
};

}
