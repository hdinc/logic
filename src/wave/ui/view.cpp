#include "view.h"
#include "../search.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace logic::wave {

namespace {

constexpr double max_time = double(never / 2);

time as_time(double t)
{
    return (time)std::clamp(t, 0.0, max_time);
}

constexpr ImU32 known_color = IM_COL32(80, 200, 130, 255);
constexpr ImU32 unknown_color = IM_COL32(245, 130, 80, 255);
constexpr ImU32 floating_color = IM_COL32(120, 170, 240, 255);
constexpr ImU32 cursor_a_color = IM_COL32(240, 200, 70, 255);
constexpr ImU32 cursor_b_color = IM_COL32(90, 200, 240, 255);
constexpr ImU32 marker_color = IM_COL32(200, 130, 240, 255);

ImU32 with_alpha(ImU32 c, float a)
{
    auto v = ImGui::ColorConvertU32ToFloat4(c);
    v.w = a;
    return ImGui::ColorConvertFloat4ToU32(v);
}

ImU32 change_color(const trace& t, size_t i, ImU32 custom)
{
    if (t.fully_floating(i)) return floating_color;
    if (!t.fully_known(i)) return unknown_color;
    return custom ? custom : known_color;
}

bool input_string(const char* label, std::string& s, ImGuiInputTextFlags flags = 0)
{
    char buf[256];
    s.copy(buf, sizeof(buf) - 1);
    buf[std::min(s.size(), sizeof(buf) - 1)] = 0;
    bool entered = ImGui::InputText(label, buf, sizeof(buf), flags);
    if (s != buf) s = buf;
    return entered;
}

// times to x and back within a rect
struct mapping {
    double start, span;
    float left, width;

    float x(double t) const { return left + float((t - start) / span * width); }
    double t(float px) const { return start + double(px - left) / width * span; }
};

void dashed_line(ImDrawList& d, float x, float y0, float y1, ImU32 c)
{
    for (float y = y0; y < y1; y += 8)
        d.AddLine({x, y}, {x, std::min(y + 4, y1)}, c);
}

// changes closer than a pixel and a half are drawn as one block, found by
// binary search: the cost is bounded by the width, not the number of
// changes
void draw_digital(ImDrawList& draw, const trace& tr, format f, const mapping& m, time end, float top, float height, ImU32 custom)
{
    using namespace ImGui;
    size_t n = tr.size();
    if (n == 0) return;
    float left = m.left, right = m.left + m.width;
    float high = top + 7, low = top + height - 7, mid = (high + low) / 2;
    bool wire = tr.type() == kind::digital && tr.width() == 1;

    size_t i = tr.index_at(as_time(m.start));
    if (i == trace::npos) i = 0;
    float dense_a = 0, dense_b = -1;
    bool dense_known = true;
    auto flush = [&] {
        if (dense_b < dense_a) return;
        auto c = dense_known ? (custom ? custom : known_color) : unknown_color;
        draw.AddRectFilled({dense_a, high}, {std::max(dense_b, dense_a + 1), low}, c);
        dense_b = -1;
        dense_known = true;
    };

    while (i < n) {
        time ta = tr.at(i), tb = i + 1 < n ? tr.at(i + 1) : std::max(end, ta);
        float raw_a = m.x(double(ta)), raw_b = m.x(double(tb));
        if (raw_a > right) break;
        float a = std::clamp(raw_a, left, right), b = std::clamp(raw_b, left, right);
        if (b - a < 1.5f && i + 1 < n && raw_b >= left) {
            if (dense_b < dense_a) dense_a = a;
            dense_known &= tr.fully_known(i);
            // on to the last change before the next pixel and a half
            size_t j = tr.lower_bound(as_time(std::ceil(m.t(a + 1.5f))));
            size_t last = std::max(i + 1, j > 0 ? j - 1 : 0);
            dense_b = std::clamp(m.x(double(tr.at(last))), left, right);
            i = last;
            continue;
        }
        if (b <= a) {
            i++;
            continue;
        }
        flush();

        auto color = change_color(tr, i, custom);
        std::string text;
        auto label = [&]() -> const std::string& {
            if (text.empty()) text = format_change(tr, i, f);
            return text;
        };
        if (wire) {
            auto s = tr.bit(i, 0);
            float y = s == sim::bit_state::one ? high : s == sim::bit_state::zero ? low : mid;
            draw.AddLine({a, y}, {b, y}, color, 2);
            if (i > 0 && raw_a >= left) draw.AddLine({a, high}, {a, low}, color);
            if (!tr.fully_known(i) && b - a > GetFontSize() + 8 && CalcTextSize(label().c_str()).x + 8 < b - a)
                draw.AddText({a + 4, mid - GetTextLineHeight()}, color, label().c_str());
        } else {
            // neighboring hexagons meet at the change, their sloping edges
            // form an X
            float rise = i > 0 && raw_a >= left ? std::min(5.0f, (b - a) * 0.3f) : 0;
            float fall = i + 1 < n && raw_b <= right ? std::min(5.0f, (b - a) * 0.3f) : 0;
            ImVec2 polygon[] = {{a + rise, high}, {b - fall, high}, {b, mid}, {b - fall, low}, {a + rise, low}, {a, mid}};
            draw.AddConvexPolyFilled(polygon, 6, with_alpha(color, 0.18f));
            draw.AddPolyline(polygon, 6, color, ImDrawFlags_Closed, 1.5f);
            float inset = std::max(4.0f, rise + 2);
            if (b - a > GetFontSize() + inset + 4 && CalcTextSize(label().c_str()).x + inset + std::max(4.0f, fall) < b - a)
                draw.AddText({a + inset, mid - GetTextLineHeight() / 2}, color, label().c_str());
        }
        i++;
    }
    flush();
}

void draw_analog(ImDrawList& draw, const trace& tr, display mode, bool is_signed, double lo, double hi, const mapping& m, time end, float top, float height,
    ImU32 color)
{
    size_t n = tr.size();
    if (n == 0) return;
    float y0 = top + 6, y1 = top + height - 6;
    auto y_of = [&](double v) { return hi <= lo ? (y0 + y1) / 2 : y1 - float((v - lo) / (hi - lo)) * (y1 - y0); };
    size_t i0 = tr.index_at(as_time(m.start));
    if (i0 == trace::npos) i0 = 0;
    size_t i1 = tr.index_at(as_time(m.start + m.span));
    if (i1 == trace::npos) return;
    auto x_end = [&](size_t i) { return m.x(double(i + 1 < n ? tr.at(i + 1) : std::max(end, tr.at(i)))); };

    if (i1 - i0 < size_t(m.width)) {
        std::vector<ImVec2> points;
        auto flush = [&] {
            if (points.size() >= 2) draw.AddPolyline(points.data(), (int)points.size(), color, 0, 1.5f);
            points.clear();
        };
        size_t last = std::min(i1 + 1, n - 1);
        for (size_t i = i0; i <= last; i++) {
            double v = tr.number(i, is_signed);
            float xa = m.x(double(tr.at(i))), xb = x_end(i);
            if (std::isnan(v)) {
                flush();
                draw.AddRectFilled({xa, y0}, {std::max(xb, xa + 1), y1}, with_alpha(unknown_color, 0.25f));
                continue;
            }
            float y = y_of(v);
            if (mode == display::analog_step) {
                points.push_back({xa, y});
                points.push_back({xb, y});
            } else {
                points.push_back({xa, y});
                if (i + 1 == n) points.push_back({xb, y});
            }
        }
        flush();
        return;
    }

    // more changes than pixels: the range of each column
    for (int px = 0; px < (int)m.width; px++) {
        float x = m.left + px;
        size_t a = tr.index_at(as_time(m.t(x)));
        size_t b = tr.index_at(as_time(m.t(x + 1)));
        if (b == trace::npos) continue;
        if (a == trace::npos) a = 0;
        auto [l, h] = tr.range(a, b + 1, is_signed);
        if (std::isnan(l)) {
            draw.AddLine({x, y0}, {x, y1}, with_alpha(unknown_color, 0.25f));
            continue;
        }
        draw.AddLine({x + 0.5f, y_of(h)}, {x + 0.5f, y_of(l) + 1}, color, 1.5f);
    }
}

double nice_step(double raw)
{
    double base = std::pow(10.0, std::floor(std::log10(raw)));
    double r = raw / base;
    return std::max(1.0, base * (r > 5 ? 10 : r > 2 ? 5 : r > 1 ? 2 : 1));
}

}

void view::add(const trace_set& data, int64_t key, int at)
{
    if (has(key)) return;
    auto s = data.find(key);
    if (!s) return;
    row r;
    r.key = key;
    if (s->type == kind::real) {
        r.mode = display::analog_linear;
        r.height = 3;
    }
    std::erase(removed, key);
    if (at < 0 || at > (int)m_rows.size()) m_rows.push_back(r);
    else m_rows.insert(m_rows.begin() + at, r);
}

bool view::has(int64_t key) const
{
    return std::any_of(m_rows.begin(), m_rows.end(), [key](auto& r) { return r.key == key; });
}

void view::clear()
{
    m_rows.clear();
    selection.clear();
    removed.clear();
    bits.clear();
    cursor_a.reset();
    cursor_b.reset();
    markers.clear();
}

session view::save(const trace_set& data) const
{
    session s;
    for (auto& r : m_rows) {
        auto sig = data.find(r.key);
        if (!sig) continue;
        s.rows.push_back({sig->name, r.fmt, r.mode, r.height, r.expanded, r.color});
    }
    s.cursor_a = cursor_a;
    s.cursor_b = cursor_b;
    s.markers = markers;
    s.view_start = timeline.start();
    s.view_span = timeline.span();
    s.default_format = default_format;
    s.full_path = full_path;
    return s;
}

std::vector<std::string> view::restore(const trace_set& data, const session& s)
{
    clear();
    std::vector<std::string> missing;
    for (auto& rs : s.rows) {
        auto sig = data.find_name(rs.name);
        if (!sig) {
            missing.push_back(rs.name);
            continue;
        }
        if (has(sig->key)) continue;
        row r;
        r.key = sig->key;
        r.fmt = rs.fmt;
        r.mode = rs.mode;
        r.height = std::clamp(rs.height, 0.5f, 10.0f);
        r.expanded = rs.expanded;
        r.color = rs.color;
        m_rows.push_back(r);
    }
    cursor_a = s.cursor_a;
    cursor_b = s.cursor_b;
    markers = s.markers;
    default_format = s.default_format;
    full_path = s.full_path;
    if (s.view_span > 0) {
        timeline.zoom_to(s.view_start, s.view_start + s.view_span);
        fit = false;
    }
    return missing;
}

std::pair<time, time> view::data_range(const trace_set& data) const
{
    return {data.begin, std::max(data.end, data.begin)};
}

void view::sync_rows(const trace_set& data, view_host& host)
{
    // removed rows are dropped from the selection, the bit traces and,
    // once their signal is gone, from `removed`
    std::erase_if(removed, [&](int64_t key) { return !data.find(key); });
    if (host.rows_follow_signals()) {
        for (auto& s : data.signals)
            if (!has(s.key) && std::find(removed.begin(), removed.end(), s.key) == removed.end()) add(data, s.key);
    }
    std::erase_if(m_rows, [&](auto& r) { return !data.find(r.key); });
    std::erase_if(selection, [&](auto& id) { return !has(id.first); });
}

const trace* view::line_trace(const trace_set& data, const line_id& id)
{
    auto s = data.find(id.first);
    if (!s) return nullptr;
    auto& t = data.trace_of(*s);
    if (id.second < 0) return &t;
    auto& cached = bits[id];
    if (cached.first != t.version()) cached = {t.version(), bit_trace(t, id.second)};
    return &cached.second;
}

bool view::selected(const line_id& id) const
{
    return std::find(selection.begin(), selection.end(), id) != selection.end();
}

std::vector<view::line_id> view::targets() const
{
    if (!selection.empty()) return selection;
    std::vector<line_id> all;
    for (auto& r : m_rows)
        all.push_back({r.key, -1});
    return all;
}

void view::set_cursor(bool b, time t)
{
    (b ? cursor_b : cursor_a) = t;
    timeline.reveal(double(t));
}

void view::step(const trace_set& data, bool forward, bool b)
{
    auto& cursor = b ? cursor_b : cursor_a;
    time from = cursor ? *cursor : as_time(forward ? timeline.start() : timeline.end());
    time best = never;
    for (auto& id : targets()) {
        auto t = line_trace(data, id);
        if (!t) continue;
        time c = forward ? next_change(*t, from) : prev_change(*t, from);
        if (c == never) continue;
        if (best == never || (forward ? c < best : c > best)) best = c;
    }
    if (best != never) set_cursor(b, best);
}

void view::find(const trace_set& data, bool forward)
{
    if (selection.empty()) {
        search_status = "Select a signal to search";
        return;
    }
    auto id = selection.front();
    auto sig = data.find(id.first);
    auto t = line_trace(data, id);
    if (!sig || !t) return;
    auto r = std::find_if(m_rows.begin(), m_rows.end(), [&](auto& r) { return r.key == id.first; });
    auto fmt = r != m_rows.end() && r->fmt ? *r->fmt : default_format;
    auto p = parse_pattern(search_text, t->type(), t->width(), fmt);
    if (!p) {
        search_status = "Not a value of " + display_name(sig->name, false);
        return;
    }
    time from = cursor_a ? *cursor_a : as_time(forward ? timeline.start() : timeline.end());
    time found = forward ? next_match(*t, from, *p) : prev_match(*t, from, *p);
    if (found == never) {
        search_status = "Not found";
        return;
    }
    search_status.clear();
    set_cursor(false, found);
}

void view::jump_marker(bool forward)
{
    time from = cursor_a ? *cursor_a : as_time(timeline.start() + timeline.span() / 2);
    const marker* best = nullptr;
    for (auto& m : markers) {
        if (forward ? m.t <= from : m.t >= from) continue;
        if (!best || (forward ? m.t < best->t : m.t > best->t)) best = &m;
    }
    if (best) set_cursor(false, best->t);
}

void view::remove_row(int index, view_host& host)
{
    auto key = m_rows[index].key;
    m_rows.erase(m_rows.begin() + index);
    std::erase_if(selection, [key](auto& id) { return id.first == key; });
    removed.push_back(key);
    host.removed(key);
}

void view::draw(const trace_set& data, view_host& host)
{
    using namespace ImGui;
    if (!started) {
        started = true;
        timeline.follow = host.live();
    }
    sync_rows(data, host);
    if (generation != data.generation) {
        // the history started over
        if (generation != ~uint64_t(0)) {
            cursor_a.reset();
            cursor_b.reset();
        }
        generation = data.generation;
        fit = true;
    }
    if (fit) {
        auto [first, last] = data_range(data);
        timeline.fit(first, last);
        fit = false;
    }

    PushID(this);
    toolbar(data, host);
    status_line(data);
    float overview_height = GetTextLineHeight() + 12;
    float plot_height = std::max(60.0f, GetContentRegionAvail().y - overview_height - GetStyle().ItemSpacing.y);
    plot(data, host, plot_height);
    overview(data, overview_height);
    shortcuts(data);
    PopID();
}

void view::toolbar(const trace_set& data, view_host& host)
{
    using namespace ImGui;
    host.toolbar();

    if (Button("Fit")) {
        fit = true;
        timeline.follow = false;
    }
    SetItemTooltip("Show everything (F)");
    SameLine(0, 2);
    if (Button("-")) timeline.zoom(-2, 0.5);
    SetItemTooltip("Zoom out (-)");
    SameLine(0, 2);
    if (Button("+")) timeline.zoom(2, 0.5);
    SetItemTooltip("Zoom in (+), or drag over the time ruler");
    if (host.live()) {
        SameLine();
        Checkbox("Follow", &timeline.follow);
        SetItemTooltip("Keep the latest time in view");
    }

    SameLine(0, 16);
    if (ArrowButton("prev_change", ImGuiDir_Left)) step(data, false, false);
    SetItemTooltip("Cursor to the previous transition of the selected signals (Left, Shift for cursor B)");
    SameLine(0, 2);
    if (ArrowButton("next_change", ImGuiDir_Right)) step(data, true, false);
    SetItemTooltip("Cursor to the next transition of the selected signals (Right, Shift for cursor B)");

    SameLine(0, 16);
    if (Button("Add marker")) {
        time t = cursor_a ? *cursor_a : as_time(timeline.start() + timeline.span() / 2);
        markers.push_back({"M" + std::to_string(markers.size() + 1), t});
    }
    SetItemTooltip("A marker at cursor A (M). Ctrl+[ and Ctrl+] jump between markers.");
    SameLine(0, 2);
    if (Button("Markers")) OpenPopup("markers");
    if (BeginPopup("markers")) {
        if (markers.empty()) TextDisabled("No markers. Add one at cursor A with M.");
        int erase = -1;
        for (int k = 0; k < (int)markers.size(); k++) {
            auto& m = markers[k];
            PushID(k);
            SetNextItemWidth(GetFontSize() * 8);
            input_string("##name", m.name);
            SameLine();
            TextDisabled("%s", format_time(m.t, data.tb).c_str());
            SameLine();
            if (SmallButton("Go")) set_cursor(false, m.t);
            SameLine();
            if (SmallButton("Delete")) erase = k;
            PopID();
        }
        if (erase >= 0) markers.erase(markers.begin() + erase);
        EndPopup();
    }

    SameLine(0, 16);
    int f = int(default_format);
    SetNextItemWidth(GetFontSize() * 5);
    if (Combo("##format", &f, "Hex\0UInt\0Int\0Binary\0Octal\0")) default_format = format(f);
    SetItemTooltip("Value format of signals without their own");
    SameLine();
    int naming = full_path ? 0 : 1;
    SetNextItemWidth(GetFontSize() * 7);
    if (Combo("##names", &naming, "Full path\0Short names\0")) full_path = naming == 0;

    // value search
    SetNextItemWidth(GetFontSize() * 9);
    if (input_string("##find", search_text, ImGuiInputTextFlags_EnterReturnsTrue)) find(data, true);
    SetItemTooltip("A value of the selected signal in its format, 0x, 0b and 0o pick another. ? matches any digit.");
    SameLine(0, 2);
    if (Button("Find <")) find(data, false);
    SameLine(0, 2);
    if (Button("Find >")) find(data, true);
    if (!search_status.empty()) {
        SameLine();
        TextColored(ColorConvertU32ToFloat4(unknown_color), "%s", search_status.c_str());
    }
}

void view::status_line(const trace_set& data)
{
    using namespace ImGui;
    auto tb = data.tb;
    std::string text;
    if (cursor_a) text += "A: " + format_time(*cursor_a, tb);
    if (cursor_b) text += (text.empty() ? "" : "   ") + std::string("B: ") + format_time(*cursor_b, tb);
    if (cursor_a && cursor_b) {
        time d = *cursor_a > *cursor_b ? *cursor_a - *cursor_b : *cursor_b - *cursor_a;
        text += "   B-A: " + std::string(*cursor_b < *cursor_a ? "-" : "") + format_time(d, tb);
        auto f = format_frequency(double(d), tb);
        if (!f.empty()) text += " (" + f + ")";
    }
    if (text.empty()) TextDisabled("Click to set cursor A, Shift+click cursor B. Cursors snap to transitions, Alt doesn't.");
    else TextUnformatted(text.c_str());
}

void view::shortcuts(const trace_set& data)
{
    using namespace ImGui;
    auto& io = GetIO();
    if (io.WantTextInput || !IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;
    if (IsKeyPressed(ImGuiKey_LeftArrow)) step(data, false, io.KeyShift);
    if (IsKeyPressed(ImGuiKey_RightArrow)) step(data, true, io.KeyShift);
    if (io.KeyCtrl && IsKeyPressed(ImGuiKey_LeftBracket)) jump_marker(false);
    if (io.KeyCtrl && IsKeyPressed(ImGuiKey_RightBracket)) jump_marker(true);
    if (!io.KeyCtrl && IsKeyPressed(ImGuiKey_M, false)) {
        time t = cursor_a ? *cursor_a : as_time(timeline.start() + timeline.span() / 2);
        markers.push_back({"M" + std::to_string(markers.size() + 1), t});
    }
    if (!io.KeyCtrl && IsKeyPressed(ImGuiKey_F, false)) {
        fit = true;
        timeline.follow = false;
    }
    if (IsKeyPressed(ImGuiKey_Equal) || IsKeyPressed(ImGuiKey_KeypadAdd)) timeline.zoom(2, 0.5);
    if (IsKeyPressed(ImGuiKey_Minus) || IsKeyPressed(ImGuiKey_KeypadSubtract)) timeline.zoom(-2, 0.5);
    if (IsKeyPressed(ImGuiKey_Escape, false)) cursor_b.reset();
}

void view::row_menu(const trace_set& data, row& r, const signal& s)
{
    using namespace ImGui;
    TextDisabled("%s", s.name.c_str());
    if (s.type != kind::text) {
        SeparatorText("Display");
        for (int k = 0; k < 3; k++)
            if (MenuItem(display_names[k], nullptr, r.mode == display(k))) {
                r.mode = display(k);
                if (r.mode != display::digital && r.height < 2) r.height = 3;
                if (r.mode == display::digital && r.height > 2) r.height = 1;
            }
    }
    if (s.type == kind::digital) {
        SeparatorText(r.mode == display::digital ? "Format" : "Read as");
        if (MenuItem("Default", nullptr, !r.fmt)) r.fmt.reset();
        for (int f = 0; f < IM_ARRAYSIZE(format_names); f++)
            if (MenuItem(format_names[f], nullptr, r.fmt == format(f))) r.fmt = format(f);
    }
    if (r.mode != display::digital) {
        Separator();
        MenuItem("Scale to the visible part", nullptr, &r.fit_visible);
    }
    Separator();
    SetNextItemWidth(GetFontSize() * 8);
    SliderFloat("Height", &r.height, 0.5f, 8.0f, "%.1f");
    ImVec4 c = r.color ? ColorConvertU32ToFloat4(r.color) : ColorConvertU32ToFloat4(known_color);
    if (ColorEdit3("Color", &c.x, ImGuiColorEditFlags_NoInputs)) r.color = ColorConvertFloat4ToU32(c);
    if (r.color) {
        SameLine();
        if (SmallButton("Default")) r.color = 0;
    }
    if (s.type == kind::digital && s.width > 1) MenuItem("Expand bits", nullptr, &r.expanded);
    (void)data;
}

void view::plot(const trace_set& data, view_host& host, float plot_height)
{
    using namespace ImGui;
    auto& io = GetIO();

    BeginChild("plot", {0, plot_height}, ImGuiChildFlags_Borders);
    auto origin = GetCursorScreenPos();
    auto avail = GetContentRegionAvail();
    avail.x = std::max(1.0f, avail.x);
    float base_height = std::max(40.0f, GetTextLineHeightWithSpacing() * 2.0f);
    float axis_height = GetTextLineHeightWithSpacing() * 2.0f;
    float axis_y = origin.y + GetScrollY();
    float visible_height = GetWindowHeight() - GetStyle().WindowPadding.y * 2;

    // the lines in content coordinates below the ruler
    std::vector<line> lines;
    float y = 0;
    for (int k = 0; k < (int)m_rows.size(); k++) {
        auto& r = m_rows[k];
        float h = base_height * r.height;
        lines.push_back({k, -1, y, h});
        y += h;
        auto s = data.find(r.key);
        if (r.expanded && s && s->type == kind::digital && s->width > 1)
            for (int b = s->width - 1; b >= 0; b--) {
                lines.push_back({k, b, y, base_height * 0.8f});
                y += base_height * 0.8f;
            }
    }
    float height = std::max(avail.y, axis_height + y);

    auto draw = GetWindowDrawList();
    auto text_color = GetColorU32(ImGuiCol_Text);
    auto muted = GetColorU32(ImGuiCol_TextDisabled);
    auto border = GetColorU32(ImGuiCol_Border);

    float min_names = std::min(100.0f, avail.x * 0.5f);
    float max_names = std::max(min_names, avail.x - 90.0f);
    // name_width is kept when the window is too small for it
    float names = std::clamp(name_width, min_names, max_names);
    constexpr float divider = 6;
    SetCursorScreenPos({origin.x + names, axis_y});
    InvisibleButton("divider", {divider, std::max(1.0f, visible_height)});
    bool resizing = IsItemActive(), resize_hovered = IsItemHovered();
    if (resizing) name_width = names = std::clamp(names + io.MouseDelta.x, min_names, max_names);
    if (resizing || resize_hovered) SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    float left = origin.x + names + divider;
    float width = std::max(1.0f, avail.x - names - divider);

    // the ruler, dragging over it zooms to that range
    SetCursorScreenPos({left, axis_y});
    InvisibleButton("ruler", {width, axis_height});
    if (IsItemActivated()) region_from = timeline.start() + double(io.MousePos.x - left) / width * timeline.span();
    if (region_from && IsItemDeactivated()) {
        double to = timeline.start() + double(io.MousePos.x - left) / width * timeline.span();
        double px = std::abs(to - *region_from) / timeline.span() * width;
        if (px > 4) timeline.zoom_to(*region_from, to);
        else cursor_a = as_time(*region_from);
        region_from.reset();
    }
    SetItemTooltip("Drag to zoom to a range, click to set cursor A");

    // the lines, clicking sets cursors
    SetCursorScreenPos({left, origin.y + axis_height});
    InvisibleButton("timeline", {width, std::max(1.0f, height - axis_height)},
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    SetItemKeyOwner(ImGuiKey_MouseWheelY);
    bool hovered = IsItemHovered(), active = IsItemActive();
    if (BeginDragDropTarget()) {
        if (auto p = AcceptDragDropPayload(signal_payload)) add(data, *(const int64_t*)p->Data);
        EndDragDropTarget();
    }
    if (hovered && io.MouseWheel != 0) timeline.zoom(io.MouseWheel, double(io.MousePos.x - left) / width);
    if (active && io.MouseDelta.x != 0 && (IsMouseDragging(ImGuiMouseButton_Right, 0) || IsMouseDragging(ImGuiMouseButton_Middle, 0)))
        timeline.pan(io.MouseDelta.x / width);
    timeline.update(io.DeltaTime, data.end, host.running());
    mapping m{timeline.start(), timeline.span(), left, width};

    // the line under the mouse
    int hovered_line = -1;
    if (hovered) {
        float my = io.MousePos.y - origin.y - axis_height;
        for (int k = 0; k < (int)lines.size(); k++)
            if (my >= lines[k].top && my < lines[k].top + lines[k].height) hovered_line = k;
    }
    auto line_key = [&](const line& l) -> line_id { return {m_rows[l.row].key, l.bit}; };

    if (active && IsMouseDown(ImGuiMouseButton_Left)) {
        double t = m.t(io.MousePos.x);
        time c = as_time(t);
        // to the nearest transition of the line within a few pixels
        if (!io.KeyAlt && hovered_line >= 0) {
            if (auto tr = line_trace(data, line_key(lines[hovered_line]))) {
                time candidates[] = {prev_change(*tr, as_time(t) + 1), next_change(*tr, as_time(t))};
                float best = 8;
                for (time cand : candidates) {
                    if (cand == never) continue;
                    float d = std::abs(m.x(double(cand)) - io.MousePos.x);
                    if (d <= best) {
                        best = d;
                        c = cand;
                    }
                }
            }
        }
        (io.KeyShift ? cursor_b : cursor_a) = c;
    }

    // ruler ticks
    draw->PushClipRect({left, origin.y}, {left + width, origin.y + height}, true);
    double tick = nice_step(m.span / std::max(1.0, double(width) / 110.0));
    double first_tick = std::ceil(m.start / tick) * tick;
    for (int k = 0; k < 200; k++) {
        double t = first_tick + k * tick;
        if (t > m.start + m.span) break;
        float x = m.x(t);
        draw->AddLine({x, axis_y + axis_height}, {x, origin.y + height}, with_alpha(border, 0.5f));
        draw->AddText({x + 3, axis_y + 3}, muted, format_time(as_time(t), data.tb).c_str());
    }
    draw->PopClipRect();
    draw->PushClipRect({origin.x, axis_y}, {left - divider, axis_y + axis_height}, true);
    draw->AddText({origin.x + 3, axis_y + 3}, muted, "Signal / value");
    draw->PopClipRect();

    time inspect_at = cursor_a ? *cursor_a : data.end;
    int remove = -1;
    int move_from = -1, move_to = -1;
    std::vector<std::pair<int64_t, int>> drops;
    float lines_top = axis_y + axis_height;
    float lines_bottom = axis_y + visible_height;
    PushClipRect({origin.x, lines_top}, {left + width, lines_bottom}, true);
    for (int k = 0; k < (int)lines.size(); k++) {
        auto& l = lines[k];
        float top = origin.y + axis_height + l.top;
        if (top + l.height < lines_top || top > lines_bottom) continue;
        auto& r = m_rows[l.row];
        auto sig = data.find(r.key);
        if (!sig) continue;
        auto id = line_key(l);
        auto tr = line_trace(data, id);
        if (!tr) continue;
        auto fmt = r.fmt.value_or(default_format);
        bool sel = selected(id);

        if (sel) draw->AddRectFilled({origin.x, top}, {left - divider, top + l.height}, GetColorU32(ImGuiCol_Header));
        draw->AddLine({origin.x, top}, {left + width, top}, with_alpha(border, 0.6f));

        PushID(k);
        float text_x = origin.x + GetFontSize() * 2;
        if (l.bit < 0) {
            SetCursorScreenPos({origin.x + 2, top + 3});
            PushStyleVar(ImGuiStyleVar_FramePadding, {2, 1});
            if (SmallButton("x")) remove = l.row;
            SetItemTooltip("Remove (Delete)");
            text_x = GetItemRectMax().x + 5;
            if (sig->type == kind::digital && sig->width > 1) {
                SameLine(0, 2);
                if (ArrowButton("expand", r.expanded ? ImGuiDir_Down : ImGuiDir_Right)) r.expanded = !r.expanded;
                SetItemTooltip(r.expanded ? "Collapse bits" : "Expand into bits");
                text_x = GetItemRectMax().x + 5;
            }
            PopStyleVar();
        }
        SetCursorScreenPos({text_x, top});
        InvisibleButton("name", {std::max(1.0f, origin.x + names - text_x - 4), l.height});
        if (IsItemClicked(ImGuiMouseButton_Left) || IsItemClicked(ImGuiMouseButton_Right)) {
            if (io.KeyCtrl) {
                if (sel) std::erase(selection, id);
                else selection.push_back(id);
            } else if (!sel || IsItemClicked(ImGuiMouseButton_Left)) {
                selection = {id};
            }
        }
        if (l.bit < 0 && BeginDragDropSource()) {
            int index = l.row;
            SetDragDropPayload("WAVE_ROW", &index, sizeof(index));
            TextUnformatted(sig->name.c_str());
            EndDragDropSource();
        }
        if (BeginDragDropTarget()) {
            if (auto p = AcceptDragDropPayload("WAVE_ROW")) {
                move_from = *(const int*)p->Data;
                move_to = l.row;
            }
            if (auto p = AcceptDragDropPayload(signal_payload)) drops.push_back({*(const int64_t*)p->Data, l.row});
            EndDragDropTarget();
        }
        if (l.bit < 0 && BeginPopupContextItem("row_menu")) {
            row_menu(data, r, *sig);
            Separator();
            if (MenuItem("Remove", "Delete")) remove = l.row;
            EndPopup();
        }
        if (IsItemHovered() && !IsPopupOpen("row_menu")) SetTooltip("%s\nRight-click for display options, drag to reorder", sig->name.c_str());

        std::string name = l.bit < 0 ? display_name(sig->name, full_path) : "[" + std::to_string(l.bit) + "]";
        std::string value = "-";
        size_t at = tr->index_at(inspect_at);
        if (at != trace::npos) value = l.bit >= 0 ? sim::to_string(tr->value(at)) : format_change(*tr, at, fmt);
        draw->PushClipRect({text_x, top}, {origin.x + names - 4, top + l.height}, true);
        draw->AddText({text_x, top + 2}, text_color, name.c_str());
        draw->AddText({text_x, top + GetTextLineHeightWithSpacing()}, muted, value.c_str());
        draw->PopClipRect();
        PopID();

        // the trace
        draw->PushClipRect({left, top}, {left + width, top + l.height}, true);
        if (r.mode != display::digital && l.bit < 0 && tr->type() != kind::text) {
            bool is_signed = fmt == format::sint;
            size_t i0 = tr->index_at(as_time(m.start)), i1 = tr->index_at(as_time(m.start + m.span));
            auto range = r.fit_visible ? tr->range(i0 == trace::npos ? 0 : i0, i1 == trace::npos ? 0 : i1 + 1, is_signed)
                                       : tr->range(0, tr->size(), is_signed);
            double lo = range.first, hi = range.second;
            if (std::isnan(lo)) lo = hi = 0;
            if (hi <= lo) {
                double pad = std::max(1.0, std::abs(lo) * 0.1);
                lo -= pad;
                hi += pad;
            }
            draw_analog(*draw, *tr, r.mode, is_signed, lo, hi, m, data.end, top, l.height, r.color ? r.color : known_color);
            draw->AddText({left + 3, top + 2}, muted, format_real(hi).c_str());
            draw->AddText({left + 3, top + l.height - GetTextLineHeight() - 2}, muted, format_real(lo).c_str());
        } else {
            draw_digital(*draw, *tr, fmt, m, data.end, top, l.height, r.color);
        }
        draw->PopClipRect();

        if (k == hovered_line && hovered && !IsMouseDown(ImGuiMouseButton_Left)) {
            size_t i = tr->index_at(as_time(m.t(io.MousePos.x)));
            if (i != trace::npos)
                SetTooltip("%s%s\n%s: %s", sig->name.c_str(), l.bit >= 0 ? name.c_str() : "", format_time(tr->at(i), data.tb).c_str(),
                    format_change(*tr, i, fmt).c_str());
        }
    }
    PopClipRect();

    // cursors and markers over everything
    draw->PushClipRect({left, axis_y}, {left + width, axis_y + visible_height}, true);
    if (cursor_a && cursor_b) {
        float xa = m.x(double(*cursor_a)), xb = m.x(double(*cursor_b));
        draw->AddRectFilled({std::min(xa, xb), lines_top}, {std::max(xa, xb), lines_bottom}, with_alpha(cursor_b_color, 0.08f));
        time d = *cursor_a > *cursor_b ? *cursor_a - *cursor_b : *cursor_b - *cursor_a;
        auto label = format_time(d, data.tb);
        auto f = format_frequency(double(d), data.tb);
        if (!f.empty()) label += "  " + f;
        auto size = CalcTextSize(label.c_str());
        float cx = (xa + xb) / 2 - size.x / 2;
        draw->AddRectFilled({cx - 4, lines_top + 2}, {cx + size.x + 4, lines_top + size.y + 4}, GetColorU32(ImGuiCol_PopupBg));
        draw->AddText({cx, lines_top + 3}, cursor_b_color, label.c_str());
    }
    for (auto& mk : markers) {
        float x = m.x(double(mk.t));
        dashed_line(*draw, x, axis_y + axis_height / 2, lines_bottom, marker_color);
        draw->AddText({x + 3, axis_y + axis_height / 2}, marker_color, mk.name.c_str());
    }
    if (cursor_a) draw->AddLine({m.x(double(*cursor_a)), axis_y}, {m.x(double(*cursor_a)), lines_bottom}, cursor_a_color, 1.5f);
    if (cursor_b) draw->AddLine({m.x(double(*cursor_b)), axis_y}, {m.x(double(*cursor_b)), lines_bottom}, cursor_b_color, 1.5f);
    if (region_from && IsMouseDown(ImGuiMouseButton_Left)) {
        float xa = m.x(*region_from), xb = io.MousePos.x;
        draw->AddRectFilled({std::min(xa, xb), axis_y}, {std::max(xa, xb), lines_bottom}, with_alpha(cursor_a_color, 0.12f));
    }
    draw->PopClipRect();
    draw->AddRectFilled({left - divider, axis_y}, {left, axis_y + visible_height},
        GetColorU32(resizing ? ImGuiCol_SeparatorActive : resize_hovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator));

    if (m_rows.empty()) {
        SetCursorScreenPos({left + 10, lines_top + 10});
        PushTextWrapPos(left + width - 10);
        TextDisabled("%s", host.rows_follow_signals() ? "No signals are recorded. Add some from the simulation's Signals menu, or right-click a wire."
                                                       : "Drag signals here, or double-click them in the signal list.");
        PopTextWrapPos();
    }

    SetCursorScreenPos({origin.x, origin.y + height});
    Dummy({1, 1});
    EndChild();

    if (remove < 0 && IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput && IsKeyPressed(ImGuiKey_Delete, false)) {
        for (int k = (int)m_rows.size() - 1; k >= 0; k--)
            if (selected({m_rows[k].key, -1})) remove_row(k, host);
    }
    if (remove >= 0) remove_row(remove, host);
    if (move_from >= 0 && move_to >= 0 && move_from != move_to && move_from < (int)m_rows.size()) {
        auto r = m_rows[move_from];
        m_rows.erase(m_rows.begin() + move_from);
        m_rows.insert(m_rows.begin() + std::min(move_to, (int)m_rows.size()), r);
    }
    for (auto [key, at] : drops)
        add(data, key, at);
}

void view::overview(const trace_set& data, float bar_height)
{
    using namespace ImGui;
    auto& io = GetIO();
    auto [first, last] = data_range(data);
    double lo = std::min(double(first), timeline.start());
    double hi = std::max(double(last), timeline.end());
    if (hi <= lo) hi = lo + 1;

    auto pos = GetCursorScreenPos();
    float width = std::max(1.0f, GetContentRegionAvail().x);
    InvisibleButton("overview", {width, bar_height});
    auto x_of = [&](double t) { return pos.x + float((t - lo) / (hi - lo) * width); };
    auto t_of = [&](float x) { return lo + double(x - pos.x) / width * (hi - lo); };

    float wa = x_of(timeline.start()), wb = std::max(x_of(timeline.end()), wa + 3);
    if (IsItemActivated()) {
        double t = t_of(io.MousePos.x);
        bool inside = io.MousePos.x >= wa && io.MousePos.x <= wb;
        overview_grab = inside ? t - timeline.start() : timeline.span() / 2;
    }
    if (IsItemActive() && overview_grab) timeline.set_start(t_of(io.MousePos.x) - *overview_grab);
    if (IsItemDeactivated()) overview_grab.reset();
    SetItemTooltip("The whole recording, the bright part is in view. Click or drag to move the view.");

    auto draw = GetWindowDrawList();
    draw->AddRectFilled(pos, {pos.x + width, pos.y + bar_height}, GetColorU32(ImGuiCol_FrameBg), 3);
    // where there is data
    draw->AddRectFilled({x_of(double(first)), pos.y + bar_height - 4}, {x_of(double(last)), pos.y + bar_height - 1}, with_alpha(known_color, 0.6f));
    draw->AddRectFilled({wa, pos.y}, {wb, pos.y + bar_height}, GetColorU32(IsItemActive() ? ImGuiCol_ButtonActive : ImGuiCol_Button), 3);
    for (auto& mk : markers)
        draw->AddLine({x_of(double(mk.t)), pos.y}, {x_of(double(mk.t)), pos.y + bar_height}, marker_color);
    if (cursor_a) draw->AddLine({x_of(double(*cursor_a)), pos.y}, {x_of(double(*cursor_a)), pos.y + bar_height}, cursor_a_color);
    if (cursor_b) draw->AddLine({x_of(double(*cursor_b)), pos.y}, {x_of(double(*cursor_b)), pos.y + bar_height}, cursor_b_color);

    auto view_text = format_time(as_time(timeline.start()), data.tb) + " - " + format_time(as_time(timeline.end()), data.tb) + "  (" +
        format_time(as_time(timeline.span()), data.tb) + ")";
    auto total_text = "of " + format_time(first, data.tb) + " - " + format_time(last, data.tb);
    float ty = pos.y + (bar_height - GetTextLineHeight()) / 2;
    draw->PushClipRect(pos, {pos.x + width, pos.y + bar_height}, true);
    draw->AddText({pos.x + 6, ty}, GetColorU32(ImGuiCol_Text), view_text.c_str());
    float tw = CalcTextSize(total_text.c_str()).x;
    if (CalcTextSize(view_text.c_str()).x + tw + 30 < width) draw->AddText({pos.x + width - tw - 6, ty}, GetColorU32(ImGuiCol_TextDisabled), total_text.c_str());
    draw->PopClipRect();
}

}
