#include "vt100.h"

#include <algorithm>
#include <cstdio>

namespace logic::ui {

namespace {

void append_utf8(std::string& s, char32_t c)
{
    if (c < 0x80) {
        s += char(c);
    } else if (c < 0x800) {
        s += char(0xC0 | (c >> 6));
        s += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        s += char(0xE0 | (c >> 12));
        s += char(0x80 | ((c >> 6) & 0x3F));
        s += char(0x80 | (c & 0x3F));
    } else {
        s += char(0xF0 | (c >> 18));
        s += char(0x80 | ((c >> 12) & 0x3F));
        s += char(0x80 | ((c >> 6) & 0x3F));
        s += char(0x80 | (c & 0x3F));
    }
}

void append_line(std::string& out, const vt100::line& l, int from, int to)
{
    std::string s;
    for (int k = std::max(from, 0); k <= to && k < (int)l.size(); k++)
        append_utf8(s, l[size_t(k)].ch);
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    out += s;
}

}

vt100::vt100(int cols, int rows, size_t history_lines)
    : m_cols(std::max(cols, 2))
    , m_rows(std::max(rows, 2))
    , max_history(history_lines)
{
    reset();
}

vt100::color vt100::palette(int i)
{
    static const uint32_t base[16] = {0x000000, 0xCD0000, 0x00CD00, 0xCDCD00, 0x0000EE, 0xCD00CD, 0x00CDCD, 0xE5E5E5, 0x7F7F7F, 0xFF0000, 0x00FF00, 0xFFFF00,
        0x5C5CFF, 0xFF00FF, 0x00FFFF, 0xFFFFFF};
    uint32_t rgb = 0;
    i = std::clamp(i, 0, 255);
    if (i < 16) {
        rgb = base[i];
    } else if (i < 232) {
        static const int levels[6] = {0, 95, 135, 175, 215, 255};
        int k = i - 16;
        rgb = uint32_t(levels[k / 36]) << 16 | uint32_t(levels[(k / 6) % 6]) << 8 | uint32_t(levels[k % 6]);
    } else {
        uint32_t g = uint32_t(8 + 10 * (i - 232));
        rgb = g << 16 | g << 8 | g;
    }
    return 0x01000000u | rgb;
}

vt100::cell vt100::blank() const
{
    cell c;
    c.bg = pen.bg;
    return c;
}

vt100::line vt100::blank_line() const { return line(size_t(m_cols), blank()); }

void vt100::reset()
{
    pen = {};
    main_screen.assign(size_t(m_rows), blank_line());
    alt_screen.assign(size_t(m_rows), blank_line());
    history.clear();
    alt = false;
    cur_row = cur_col = 0;
    wrap_pending = false;
    saved = saved_alt = {};
    top = 0;
    bottom = m_rows - 1;
    show_cursor = autowrap = true;
    app_cursor = insert_mode = origin_mode = false;
    st = state::ground;
    params.clear();
    utf8_left = 0;
    m_version++;
}

std::string vt100::take_reply()
{
    std::string r;
    r.swap(reply);
    return r;
}

void vt100::resize(int cols, int rows)
{
    cols = std::max(cols, 2);
    rows = std::max(rows, 2);
    if (cols == m_cols && rows == m_rows) return;
    for (auto* s : {&main_screen, &alt_screen}) {
        for (auto& l : *s)
            l.resize(size_t(cols), cell{});
        // fewer rows: the top ones go, into the history for the main screen
        while ((int)s->size() > rows) {
            if (s == &main_screen && !alt) history.push_back(std::move(s->front()));
            s->erase(s->begin());
            if (s == &screen()) cur_row = std::max(cur_row - 1, 0);
        }
        while ((int)s->size() < rows)
            s->push_back(line(size_t(cols), cell{}));
    }
    for (auto& l : history)
        l.resize(size_t(cols), cell{});
    while (history.size() > max_history)
        history.pop_front();
    m_cols = cols;
    m_rows = rows;
    top = 0;
    bottom = rows - 1;
    wrap_pending = false;
    clamp_cursor();
    m_version++;
}

void vt100::clamp_cursor()
{
    cur_row = std::clamp(cur_row, 0, m_rows - 1);
    cur_col = std::clamp(cur_col, 0, m_cols - 1);
}

void vt100::feed(std::string_view bytes)
{
    for (unsigned char b : bytes) {
        if (st == state::string) {
            if (b == 0x07 || b == 0x18 || b == 0x1A) st = state::ground;
            if (b == 0x1B) st = state::string_escape;
            continue;
        }
        if (st == state::string_escape) {
            st = b == '\\' ? state::ground : state::string;
            continue;
        }
        if (b < 0x20 || b == 0x7F) {
            utf8_left = 0;
            if (b == 0x1B) {
                st = state::escape;
            } else if (b == 0x18 || b == 0x1A) {
                st = state::ground;
            } else if (b != 0x7F) {
                control(b);
            }
            continue;
        }
        switch (st) {
        case state::escape: escape(b); break;
        case state::charset: st = state::ground; break;
        case state::csi:
            if (b >= '0' && b <= '9') {
                if (!param_started) {
                    params.push_back(0);
                    param_started = true;
                }
                params.back() = std::min(params.back() * 10 + (b - '0'), 9999);
            } else if (b == ';' || b == ':') {
                if (!param_started) params.push_back(0);
                param_started = false;
            } else if (b >= 0x3C && b <= 0x3F) {
                private_marker = char(b);
            } else if (b >= 0x40 && b <= 0x7E) {
                st = state::ground;
                csi(b);
            } else if (b >= 0x80) {
                st = state::ground;
            }
            break;
        default:
            if (b < 0x80) {
                put(b);
            } else if (utf8_left && (b & 0xC0) == 0x80) {
                utf8 = utf8 << 6 | (b & 0x3F);
                if (--utf8_left == 0) put(utf8);
            } else if (b >= 0xC0 && b < 0xF8) {
                utf8_left = b < 0xE0 ? 1 : b < 0xF0 ? 2 : 3;
                utf8 = b & (b < 0xE0 ? 0x1F : b < 0xF0 ? 0x0F : 0x07);
            } else {
                utf8_left = 0;
                put(U'?');
            }
            break;
        }
    }
}

void vt100::put(char32_t c)
{
    if (wrap_pending && autowrap) {
        cur_col = 0;
        line_feed();
    }
    wrap_pending = false;
    auto& l = screen()[size_t(cur_row)];
    if (insert_mode) {
        l.insert(l.begin() + cur_col, blank());
        l.pop_back();
    }
    cell x = pen;
    x.ch = c;
    l[size_t(cur_col)] = x;
    if (cur_col == m_cols - 1)
        wrap_pending = autowrap;
    else
        cur_col++;
    m_version++;
}

void vt100::control(uint8_t c)
{
    switch (c) {
    case 0x08:
        if (cur_col > 0) cur_col--;
        wrap_pending = false;
        break;
    case 0x09:
        cur_col = std::min(m_cols - 1, (cur_col / 8 + 1) * 8);
        wrap_pending = false;
        break;
    case 0x0A:
    case 0x0B:
    case 0x0C: line_feed(); break;
    case 0x0D:
        cur_col = 0;
        wrap_pending = false;
        break;
    default: break;
    }
    m_version++;
}

void vt100::line_feed()
{
    if (cur_row == bottom)
        scroll(1);
    else if (cur_row < m_rows - 1)
        cur_row++;
    wrap_pending = false;
}

void vt100::reverse_index()
{
    if (cur_row == top)
        scroll(-1);
    else if (cur_row > 0)
        cur_row--;
    wrap_pending = false;
}

void vt100::scroll(int n)
{
    auto& s = screen();
    int height = bottom - top + 1;
    n = std::clamp(n, -height, height);
    for (; n > 0; n--) {
        // lines leaving the top of the whole main screen are kept
        if (top == 0 && !alt) {
            history.push_back(std::move(s[size_t(top)]));
            if (history.size() > max_history) history.pop_front();
        }
        s.erase(s.begin() + top);
        s.insert(s.begin() + bottom, blank_line());
    }
    for (; n < 0; n++) {
        s.erase(s.begin() + bottom);
        s.insert(s.begin() + top, blank_line());
    }
    m_version++;
}

void vt100::erase(int row, int from, int to)
{
    auto& l = screen()[size_t(row)];
    for (int k = std::max(from, 0); k <= to && k < m_cols; k++)
        l[size_t(k)] = blank();
    m_version++;
}

void vt100::escape(uint8_t c)
{
    st = state::ground;
    switch (c) {
    case '[':
        st = state::csi;
        params.clear();
        param_started = false;
        private_marker = 0;
        break;
    case ']':
    case 'P':
    case '^':
    case '_':
    case 'X': st = state::string; break;
    case '(':
    case ')':
    case '*':
    case '+':
    case '#': st = state::charset; break;
    case '7': saved = {cur_row, cur_col, pen}; break;
    case '8':
        cur_row = saved.row;
        cur_col = saved.col;
        pen = saved.pen;
        wrap_pending = false;
        clamp_cursor();
        break;
    case 'D': line_feed(); break;
    case 'E':
        cur_col = 0;
        line_feed();
        break;
    case 'M': reverse_index(); break;
    case 'c': reset(); break;
    default: break;
    }
    m_version++;
}

int vt100::param(size_t k, int def) const { return k < params.size() && params[k] > 0 ? params[k] : def; }

void vt100::set_mode(bool on)
{
    for (size_t k = 0; k < std::max<size_t>(params.size(), 1); k++) {
        int p = k < params.size() ? params[k] : 0;
        if (private_marker != '?') {
            if (p == 4) insert_mode = on;
            continue;
        }
        switch (p) {
        case 1: app_cursor = on; break;
        case 6:
            origin_mode = on;
            cur_row = on ? top : 0;
            cur_col = 0;
            break;
        case 7: autowrap = on; break;
        case 25: show_cursor = on; break;
        case 47:
        case 1047:
        case 1049:
            if (on == alt) break;
            if (p == 1049 && on) saved_alt = {cur_row, cur_col, pen};
            alt = on;
            if (on) alt_screen.assign(size_t(m_rows), blank_line());
            if (p == 1049 && !on) {
                cur_row = saved_alt.row;
                cur_col = saved_alt.col;
                pen = saved_alt.pen;
                clamp_cursor();
            }
            break;
        case 1048:
            if (on)
                saved = {cur_row, cur_col, pen};
            else {
                cur_row = saved.row;
                cur_col = saved.col;
                pen = saved.pen;
                clamp_cursor();
            }
            break;
        default: break;
        }
    }
}

void vt100::csi(uint8_t final)
{
    int n = param(0, 1);
    auto& s = screen();
    // cursor movement stays in the scroll region when it starts there
    int row_min = cur_row >= top && cur_row <= bottom ? top : 0;
    int row_max = cur_row >= top && cur_row <= bottom ? bottom : m_rows - 1;
    if (private_marker == '?' && final != 'h' && final != 'l') {
        // DECSET queries and others aren't supported
        m_version++;
        return;
    }
    switch (final) {
    case 'A': cur_row = std::max(cur_row - n, row_min); break;
    case 'B':
    case 'e': cur_row = std::min(cur_row + n, row_max); break;
    case 'C':
    case 'a': cur_col = std::min(cur_col + n, m_cols - 1); break;
    case 'D': cur_col = std::max(cur_col - n, 0); break;
    case 'E':
        cur_row = std::min(cur_row + n, row_max);
        cur_col = 0;
        break;
    case 'F':
        cur_row = std::max(cur_row - n, row_min);
        cur_col = 0;
        break;
    case 'G':
    case '`': cur_col = n - 1; break;
    case 'H':
    case 'f':
        cur_row = param(0, 1) - 1 + (origin_mode ? top : 0);
        cur_col = param(1, 1) - 1;
        break;
    case 'd': cur_row = n - 1 + (origin_mode ? top : 0); break;
    case 'J': {
        int mode = params.empty() ? 0 : params[0];
        if (mode == 0) {
            erase(cur_row, cur_col, m_cols - 1);
            for (int r = cur_row + 1; r < m_rows; r++)
                erase(r, 0, m_cols - 1);
        } else if (mode == 1) {
            for (int r = 0; r < cur_row; r++)
                erase(r, 0, m_cols - 1);
            erase(cur_row, 0, cur_col);
        } else {
            for (int r = 0; r < m_rows; r++)
                erase(r, 0, m_cols - 1);
            if (mode == 3) history.clear();
        }
        break;
    }
    case 'K': {
        int mode = params.empty() ? 0 : params[0];
        if (mode == 0) erase(cur_row, cur_col, m_cols - 1);
        if (mode == 1) erase(cur_row, 0, cur_col);
        if (mode == 2) erase(cur_row, 0, m_cols - 1);
        break;
    }
    case 'L':
    case 'M':
        // insert or delete lines: the region from the cursor down scrolls
        if (cur_row >= top && cur_row <= bottom) {
            int t = top;
            top = cur_row;
            scroll(final == 'L' ? -n : n);
            top = t;
            cur_col = 0;
        }
        break;
    case '@': {
        auto& l = s[size_t(cur_row)];
        n = std::min(n, m_cols - cur_col);
        l.insert(l.begin() + cur_col, size_t(n), blank());
        l.resize(size_t(m_cols));
        break;
    }
    case 'P': {
        auto& l = s[size_t(cur_row)];
        n = std::min(n, m_cols - cur_col);
        l.erase(l.begin() + cur_col, l.begin() + cur_col + n);
        l.resize(size_t(m_cols), blank());
        break;
    }
    case 'X': erase(cur_row, cur_col, cur_col + n - 1); break;
    case 'S': scroll(n); break;
    case 'T': scroll(-n); break;
    case 'm': sgr(); break;
    case 'r': {
        int t = param(0, 1) - 1, b = param(1, m_rows) - 1;
        if (t < b && b < m_rows) {
            top = t;
            bottom = b;
        } else {
            top = 0;
            bottom = m_rows - 1;
        }
        cur_row = origin_mode ? top : 0;
        cur_col = 0;
        break;
    }
    case 's': saved = {cur_row, cur_col, pen}; break;
    case 'u':
        cur_row = saved.row;
        cur_col = saved.col;
        pen = saved.pen;
        break;
    case 'n':
        if (param(0, 0) == 6) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "\x1b[%d;%dR", cur_row + 1 - (origin_mode ? top : 0), cur_col + 1);
            reply += buf;
        } else if (param(0, 0) == 5) {
            reply += "\x1b[0n";
        }
        break;
    case 'c':
        if (private_marker == '>')
            reply += "\x1b[>0;0;0c";
        else if (!private_marker)
            reply += "\x1b[?1;2c";
        break;
    case 'h':
    case 'l': set_mode(final == 'h'); break;
    default: break;
    }
    wrap_pending = false;
    clamp_cursor();
    m_version++;
}

void vt100::sgr()
{
    if (params.empty()) {
        pen = {};
        return;
    }
    for (size_t k = 0; k < params.size(); k++) {
        int p = params[k];
        // 38;5;n and 38;2;r;g;b
        auto extended = [&]() -> color {
            if (k + 2 < params.size() && params[k + 1] == 5) {
                k += 2;
                return palette(params[k]);
            }
            if (k + 4 < params.size() && params[k + 1] == 2) {
                color c = 0x01000000u | uint32_t(std::clamp(params[k + 2], 0, 255)) << 16 | uint32_t(std::clamp(params[k + 3], 0, 255)) << 8 |
                    uint32_t(std::clamp(params[k + 4], 0, 255));
                k += 4;
                return c;
            }
            k = params.size();
            return 0;
        };
        if (p == 0) pen = {};
        else if (p == 1) pen.attrs |= bold;
        else if (p == 2) pen.attrs |= dim;
        else if (p == 3) pen.attrs |= italic;
        else if (p == 4) pen.attrs |= underline;
        else if (p == 7) pen.attrs |= inverse;
        else if (p == 22) pen.attrs &= uint8_t(~(bold | dim));
        else if (p == 23) pen.attrs &= uint8_t(~italic);
        else if (p == 24) pen.attrs &= uint8_t(~underline);
        else if (p == 27) pen.attrs &= uint8_t(~inverse);
        else if (p >= 30 && p <= 37) pen.fg = palette(p - 30);
        else if (p == 38) pen.fg = extended();
        else if (p == 39) pen.fg = 0;
        else if (p >= 40 && p <= 47) pen.bg = palette(p - 40);
        else if (p == 48) pen.bg = extended();
        else if (p == 49) pen.bg = 0;
        else if (p >= 90 && p <= 97) pen.fg = palette(p - 90 + 8);
        else if (p >= 100 && p <= 107) pen.bg = palette(p - 100 + 8);
    }
}

std::string vt100::text(size_t first, size_t last) const { return text(first, 0, last, m_cols - 1); }

std::string vt100::text(size_t first, int first_col, size_t last, int last_col) const
{
    std::string out;
    for (size_t k = first; k <= last && k < lines(); k++) {
        append_line(out, line_at(k), k == first ? first_col : 0, k == last ? last_col : m_cols - 1);
        if (k != last) out += '\n';
    }
    return out;
}

}
