#pragma once

// a terminal emulator for the bytes a uart sends: the vt100/xterm subset
// that linux, busybox and vi use. no drawing here, the terminal window draws
// the cells.

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace logic::ui {

class vt100 {
public:
    // 0 is the default color, else 0x01RRGGBB
    using color = uint32_t;
    enum attr : uint8_t {
        bold = 1,
        dim = 2,
        underline = 4,
        inverse = 8,
        italic = 16
    };
    struct cell {
        char32_t ch = U' ';
        color fg = 0;
        color bg = 0;
        uint8_t attrs = 0;
        bool operator==(const cell&) const = default;
    };
    using line = std::vector<cell>;

    explicit vt100(int cols = 80, int rows = 24, size_t history_lines = 5000);

    void feed(std::string_view bytes);
    // answers to queries (cursor position, device attributes) for the
    // other side, taken once
    std::string take_reply();

    // keeps the text, cut or padded at the right and bottom
    void resize(int cols, int rows);
    // the state after power on, no history
    void reset();

    int cols() const { return m_cols; }
    int rows() const { return m_rows; }
    // the history (oldest first) and then the screen's rows
    size_t lines() const { return history.size() + screen().size(); }
    const line& line_at(size_t k) const { return k < history.size() ? history[k] : screen()[k - history.size()]; }
    size_t history_size() const { return history.size(); }
    // the screen's rows only
    const cell& at(int row, int col) const { return screen()[size_t(row)][size_t(col)]; }
    int cursor_row() const { return cur_row; }
    int cursor_col() const { return cur_col; }
    bool cursor_visible() const { return show_cursor; }
    // DECCKM: arrows send ESC O A instead of ESC [ A
    bool application_cursor() const { return app_cursor; }
    bool alternate_screen() const { return alt; }
    // goes up with every change, for redrawing
    uint64_t version() const { return m_version; }

    // the text of lines [first, last] with newlines between them, trailing
    // spaces dropped
    std::string text(size_t first, size_t last) const;
    // part of it: from (first, first_col) to (last, last_col) inclusive
    std::string text(size_t first, int first_col, size_t last, int last_col) const;

    // the rgb of a 256 color index
    static color palette(int index);

private:
    enum class state {
        ground,
        escape,
        // ESC ( and friends: the next byte is a charset
        charset,
        csi,
        // an OSC or other string, up to BEL or ESC backslash
        string,
        string_escape
    };

    std::vector<line>& screen() { return alt ? alt_screen : main_screen; }
    const std::vector<line>& screen() const { return alt ? alt_screen : main_screen; }
    line blank_line() const;
    cell blank() const;

    void put(char32_t c);
    void control(uint8_t c);
    void escape(uint8_t c);
    void csi(uint8_t final);
    void sgr();
    void line_feed();
    void reverse_index();
    // rows [top, bottom] of the scroll region move up (n > 0) or down
    void scroll(int n);
    void erase(int row, int from, int to);
    void set_mode(bool on);
    void clamp_cursor();
    int param(size_t k, int def) const;

    int m_cols, m_rows;
    size_t max_history;
    std::vector<line> main_screen, alt_screen;
    std::deque<line> history;
    bool alt = false;

    int cur_row = 0, cur_col = 0;
    // the next character goes to the next line first (deferred wrap)
    bool wrap_pending = false;
    cell pen;
    struct saved_cursor {
        int row = 0, col = 0;
        cell pen;
    };
    saved_cursor saved, saved_alt;
    int top = 0, bottom = 0;
    bool show_cursor = true, app_cursor = false, autowrap = true, insert_mode = false, origin_mode = false;

    state st = state::ground;
    std::vector<int> params;
    bool param_started = false;
    char private_marker = 0;
    // utf-8 being decoded
    char32_t utf8 = 0;
    int utf8_left = 0;

    std::string reply;
    uint64_t m_version = 0;
};

}
