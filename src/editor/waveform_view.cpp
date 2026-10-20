#include "waveform_view.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace logic::editor {

namespace {

constexpr double max_span = double(sim::never / 4);
constexpr double max_time = double(sim::never / 2);
constexpr const char* format_names[] = {"Hex", "UInt", "Int", "Binary", "Octal"};

sim::time as_time(double t)
{
    return (sim::time)std::clamp(t, 0.0, max_time);
}

ImU32 signal_color(const sim::value& v)
{
    if (v.fully_floating()) return IM_COL32(120, 170, 240, 255);
    if (!v.fully_known()) return IM_COL32(245, 130, 80, 255);
    return IM_COL32(80, 200, 130, 255);
}

std::string decimal(const sim::value& v)
{
    // Base 10^9 digits, consuming 32 bits at a time. Wide buses retain all
    // their bits without expensive per-character arithmetic on every bit.
    constexpr uint64_t base = 1000000000;
    std::vector<uint32_t> digits{0};
    for (int word = (v.width() + 31) / 32 - 1; word >= 0; word--) {
        uint64_t carry = uint32_t(v.bits(word / 2) >> ((word % 2) * 32));
        for (auto& digit : digits) {
            uint64_t n = (uint64_t(digit) << 32) + carry;
            digit = uint32_t(n % base);
            carry = n / base;
        }
        while (carry) {
            digits.push_back(uint32_t(carry % base));
            carry /= base;
        }
    }
    std::string result = std::to_string(digits.back());
    for (int k = (int)digits.size() - 2; k >= 0; k--) {
        auto group = std::to_string(digits[k]);
        result.append(9 - group.size(), '0');
        result += group;
    }
    return result;
}

void draw_trace(ImDrawList& draw, const sim::waveform_signal& signal, int bit,
    waveform_format format, double start, double span, sim::time now,
    float left, float width, float top, float row_height, bool hovered)
{
    using namespace ImGui;
    std::deque<sim::waveform_sample> bits;
    if (bit >= 0) {
        for (auto& sample : signal.samples) {
            auto v = sample.v.slice(bit, 1);
            // A change on another bit must not create an edge on this one.
            if (bits.empty() || bits.back().v != v) bits.push_back({sample.at, std::move(v)});
        }
    }
    auto& samples = bit >= 0 ? bits : signal.samples;
    auto x_of = [&](sim::time t) { return left + float((double(t) - start) / span * width); };
    float high = top + 7;
    float low = top + row_height - 7;
    float mid = (high + low) / 2;
    draw.PushClipRect({left, top}, {left + width, top + row_height}, true);
    for (size_t k = 0; k < samples.size(); k++) {
        auto& sample = samples[k];
        sim::time end = k + 1 < samples.size() ? samples[k + 1].at : now;
        float raw_a = x_of(sample.at), raw_b = x_of(end);
        float a = std::clamp(raw_a, left, left + width);
        float b = std::clamp(raw_b, left, left + width);
        if (b <= a) continue;
        auto color = signal_color(sample.v);
        bool sample_hovered = hovered && GetIO().MousePos.y >= top && GetIO().MousePos.y < top + row_height &&
            GetIO().MousePos.x >= a && GetIO().MousePos.x < b;
        // Dense histories have many subpixel segments. Format values only
        // when a label could fit, or when the user inspects that segment.
        std::string text;
        auto label = [&]() -> const std::string& {
            if (text.empty()) text = bit >= 0 ? sim::to_string(sample.v) : format_waveform_value(sample.v, format);
            return text;
        };
        if (sample.v.width() == 1) {
            float y = sample.v.fully_known() ? (sample.v.get(0) == sim::bit_state::one ? high : low) : mid;
            draw.AddLine({a, y}, {b, y}, color, 2);
            if (k > 0 && raw_a >= left) draw.AddLine({a, high}, {a, low}, color);
            if (!sample.v.fully_known() && b - a > GetFontSize() + 8 && CalcTextSize(label().c_str()).x + 8 < b - a)
                draw.AddText({a + 4, mid - GetTextLineHeight()}, color, label().c_str());
        } else {
            // Neighboring filled hexagons meet at the transition time. Their
            // sloping edges form an X, centered on the actual change.
            float rise = k > 0 && raw_a >= left ? std::min(5.0f, (b - a) * 0.3f) : 0;
            float fall = k + 1 < samples.size() && raw_b <= left + width ? std::min(5.0f, (b - a) * 0.3f) : 0;
            ImVec2 polygon[] = {{a + rise, high}, {b - fall, high}, {b, mid},
                {b - fall, low}, {a + rise, low}, {a, mid}};
            auto fill = ColorConvertU32ToFloat4(color);
            fill.w = 0.18f;
            draw.AddConvexPolyFilled(polygon, 6, GetColorU32(fill));
            draw.AddPolyline(polygon, 6, color, ImDrawFlags_Closed, 1.5f);
            float inset = std::max(4.0f, rise + 2);
            if (b - a > GetFontSize() + inset + 4 && CalcTextSize(label().c_str()).x + inset + std::max(4.0f, fall) < b - a)
                draw.AddText({a + inset, mid - GetTextLineHeight() / 2}, color, label().c_str());
        }
        if (sample_hovered) {
            auto name = signal.name + (bit >= 0 ? "[" + std::to_string(bit) + "]" : "");
            SetTooltip("%s\n%s: %s", name.c_str(), sim::format_time(sample.at).c_str(), label().c_str());
        }
    }
    draw.PopClipRect();
}

}

std::string format_waveform_value(const sim::value& v, waveform_format format)
{
    if (v.width() == 0) return "-";
    if (format == waveform_format::uint || format == waveform_format::sint) {
        if (!v.fully_known()) return sim::to_string(v);
        if (format == waveform_format::sint && v.get(v.width() - 1) == sim::bit_state::one) {
            auto magnitude = sim::known(0, v.width());
            bool carry = true;
            for (int k = 0; k < v.width(); k++) {
                bool inverted = v.get(k) != sim::bit_state::one;
                magnitude.set(k, inverted != carry ? sim::bit_state::one : sim::bit_state::zero);
                carry = inverted && carry;
            }
            return "-" + decimal(magnitude);
        }
        return decimal(v);
    }
    if (v.width() == 1) return sim::to_string(v);
    int group = format == waveform_format::hex ? 4 : format == waveform_format::octal ? 3 : 1;
    std::string result;
    for (int first = ((v.width() - 1) / group) * group; first >= 0; first -= group) {
        int digit = 0;
        bool unknown = false, floating = true;
        for (int k = 0; k < group && first + k < v.width(); k++) {
            auto state = v.get(first + k);
            if (state == sim::bit_state::one) digit |= 1 << k;
            unknown |= state == sim::bit_state::x || state == sim::bit_state::z;
            floating &= state == sim::bit_state::z;
        }
        result += floating ? 'z' : unknown ? 'x' : "0123456789ABCDEF"[digit];
    }
    return (group == 4 ? "0x" : group == 3 ? "0o" : "0b") + result;
}

std::string waveform_signal_name(std::string_view name, bool full_path)
{
    if (!full_path) {
        auto last = name.find_last_of("/.");
        if (last != std::string_view::npos) name.remove_prefix(last + 1);
    }
    return std::string(name);
}

void waveform_timeline::zoom(double wheel, double fraction)
{
    anchor_fraction = std::clamp(fraction, 0.0, 1.0);
    anchor = m_start + m_span * anchor_fraction;
    target_span = std::clamp(target_span * std::pow(1.25, -wheel), 1.0, max_span);
}

void waveform_timeline::pan(double fraction)
{
    m_start = std::clamp(m_start - fraction * m_span, 0.0, max_time - m_span);
    target_span = m_span;
    follow = false;
}

void waveform_timeline::fit(sim::time first, sim::time last)
{
    if (last > first) m_span = std::clamp(double(last - first) * 1.05, 1.0, max_span);
    m_start = std::clamp(double(first), 0.0, max_time - m_span);
    target_span = m_span;
}

void waveform_timeline::update(double dt, sim::time now, bool running)
{
    if (m_span != target_span) {
        double k = std::log(target_span / m_span);
        double t = 1.0 - std::exp(-std::max(0.0, dt) * 20.0);
        m_span = std::abs(k) < 0.001 ? target_span : m_span * std::exp(k * t);
        if (!follow || !running) m_start = anchor - m_span * anchor_fraction;
    }
    if (follow && running) {
        m_start = double(now) - m_span * 0.95;
        anchor = std::max(0.0, m_start) + m_span * anchor_fraction;
    }
    m_start = std::clamp(m_start, 0.0, max_time - m_span);
}

void waveform_view::gui(sim::runner& run, const std::string& title, model::id id)
{
    using namespace ImGui;
    if (!visible) return;
    SetNextWindowSize({1000, 420}, ImGuiCond_FirstUseEver);
    auto label = "Waveforms: " + title + "###waveforms" + std::to_string(id);
    if (!Begin(label.c_str(), &visible)) {
        End();
        return;
    }

    if (Button(run.running() ? "Pause" : "Run")) run.run(!run.running());
    SameLine();
    if (Button("Step")) run.step();
    SameLine();
    if (Button("Fit")) { fit = true; timeline.follow = false; }
    SameLine();
    Checkbox("Follow", &timeline.follow);
    SameLine();
    if (Button("Clear history")) run.post([](sim::simulator& s) { s.waveforms.clear(s); });
    SameLine();
    TextDisabled("%s", run.running() ? "Recording" : "Paused");

    int format = int(default_format);
    SetNextItemWidth(105);
    if (Combo("Format", &format, "Hex\0UInt\0Int\0Binary\0Octal\0")) default_format = waveform_format(format);
    SameLine();
    int naming = full_path ? 0 : 1;
    SetNextItemWidth(140);
    if (Combo("Names", &naming, "Instance path\0Short names\0")) full_path = naming == 0;
    TextDisabled("Wheel: zoom | Right/middle drag: pan | Left drag: cursor | Right-click a name: format");

    auto& recorder = run.sim().waveforms;
    auto meta = recorder.read(0, 0);
    if (generation != meta.generation) {
        generation = meta.generation;
        fit = true;
        has_cursor = false;
    }
    if (fit && !meta.signals.empty()) {
        sim::time first = meta.now;
        for (auto& signal : meta.signals) first = std::min(first, signal.first);
        timeline.fit(first, meta.now);
        fit = false;
    }
    bool truncated = std::any_of(meta.signals.begin(), meta.signals.end(), [](auto& signal) { return signal.truncated; });
    if (truncated) TextDisabled("History retains the newest %zu changes per signal; earlier data was discarded.", sim::waveform_recorder::max_samples);
    if (has_cursor) Text("Cursor: %s", sim::format_time(cursor).c_str());
    else TextUnformatted("Drag the divider to resize the signal list. Expand a bus to see its bits.");

    if (meta.signals.empty()) {
        TextWrapped("Add signals from the simulation's Signals menu, or right-click a wire, port or instance pin and choose Add to waveforms. Recording starts when a signal is added.");
        End();
        return;
    }

    struct display_row { int net; int bit; };
    std::vector<display_row> rows;
    for (auto& signal : meta.signals) {
        rows.push_back({signal.net, -1});
        if (options[signal.net].expanded)
            for (int bit = run.sim().net_width(signal.net) - 1; bit >= 0; bit--) rows.push_back({signal.net, bit});
    }

    BeginChild("plot", {0, 0}, ImGuiChildFlags_Borders);
    auto origin = GetCursorScreenPos();
    auto avail = GetContentRegionAvail();
    avail.x = std::max(1.0f, avail.x);
    float row_height = std::max(42.0f, GetTextLineHeightWithSpacing() * 2.0f);
    float axis_height = GetTextLineHeightWithSpacing() * 2.0f;
    float axis_y = origin.y + GetScrollY();
    float height = std::max(avail.y, axis_height + row_height * rows.size());
    float visible_height = GetWindowHeight() - GetStyle().WindowPadding.y * 2;
    auto& io = GetIO();
    auto draw = GetWindowDrawList();
    auto text_color = GetColorU32(ImGuiCol_Text);
    auto muted = GetColorU32(ImGuiCol_TextDisabled);

    float min_names = std::min(100.0f, avail.x * 0.5f);
    float max_names = std::max(min_names, avail.x - 90.0f);
    float name_width = std::clamp(signal_width, min_names, max_names);
    constexpr float divider = 6;
    SetCursorScreenPos({origin.x + name_width, axis_y});
    InvisibleButton("signal_divider", {divider, std::max(1.0f, visible_height)});
    bool resizing = IsItemActive(), resize_hovered = IsItemHovered();
    if (resizing) {
        signal_width = std::clamp(name_width + io.MouseDelta.x, min_names, max_names);
        name_width = signal_width;
    }
    if (resizing || resize_hovered) SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    SetItemTooltip("Drag to resize the signal list");
    float left = origin.x + name_width + divider;
    float width = std::max(1.0f, avail.x - name_width - divider);

    SetCursorScreenPos({left, origin.y});
    InvisibleButton("timeline", {width, height}, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    SetItemKeyOwner(ImGuiKey_MouseWheelY);
    bool hovered = IsItemHovered(), active = IsItemActive();
    if (hovered && io.MouseWheel != 0)
        timeline.zoom(io.MouseWheel, double(io.MousePos.x - left) / width);
    if (active && io.MouseDelta.x != 0 && (IsMouseDragging(ImGuiMouseButton_Right, 0) || IsMouseDragging(ImGuiMouseButton_Middle, 0)))
        timeline.pan(io.MouseDelta.x / width);
    timeline.update(io.DeltaTime, meta.now, run.running());
    double start = timeline.start(), span = timeline.span();
    if (active && IsMouseDown(ImGuiMouseButton_Left)) {
        cursor = as_time(start + span * std::clamp(double(io.MousePos.x - left) / width, 0.0, 1.0));
        has_cursor = true;
    }
    auto data = recorder.read(as_time(start), as_time(start + span));
    auto inspect_at = has_cursor ? cursor : data.now;
    auto values = recorder.read(inspect_at, inspect_at);
    auto x_of = [&](sim::time t) { return left + float((double(t) - start) / span * width); };

    draw->PushClipRect({left, origin.y}, {left + width, origin.y + height}, true);
    double raw_tick = span / std::max(1.0, double(width) / 110.0);
    double base = std::pow(10.0, std::floor(std::log10(raw_tick)));
    double step = std::max(1.0, base * (raw_tick / base > 5 ? 10 : raw_tick / base > 2 ? 5 : raw_tick / base > 1 ? 2 : 1));
    double first_tick = std::ceil(start / step) * step;
    for (int k = 0; k < 200; k++) {
        double t = first_tick + k * step;
        if (t > start + span) break;
        float x = x_of(as_time(t));
        draw->AddLine({x, axis_y + axis_height}, {x, origin.y + height}, GetColorU32(ImGuiCol_Border));
        auto text = sim::format_time(as_time(t));
        draw->AddText({x + 3, axis_y + 3}, muted, text.c_str());
    }
    draw->PopClipRect();
    draw->PushClipRect({origin.x, axis_y}, {left - divider, axis_y + axis_height}, true);
    draw->AddText({origin.x + 3, axis_y + 3}, muted, "Signal / value");
    draw->PopClipRect();

    int remove_net = -1;
    // Cull offscreen rows even when a wide bus is expanded into thousands of bits.
    int first_row = std::max(0, int(GetScrollY() / row_height));
    int last_row = std::min(int(rows.size()), int(std::ceil((GetScrollY() + visible_height - axis_height) / row_height)));
    PushClipRect({origin.x, axis_y + axis_height}, {left + width, origin.y + height}, true);
    for (int row = first_row; row < last_row; row++) {
        auto [net, bit] = rows[row];
        auto found = std::find_if(data.signals.begin(), data.signals.end(), [net](auto& s) { return s.net == net; });
        if (found == data.signals.end()) continue; // a worker command removed it
        auto& signal = *found;
        auto& settings = options[net];
        auto selected_format = settings.format.value_or(default_format);
        float top = origin.y + axis_height + row * row_height;
        draw->AddLine({origin.x, top}, {left + width, top}, GetColorU32(ImGuiCol_Border));

        PushID(net);
        PushID(bit);
        float text_x = origin.x + GetFontSize() * 2;
        if (bit < 0) {
            SetCursorScreenPos({origin.x, top + 3});
            PushStyleVar(ImGuiStyleVar_FramePadding, {2, 1});
            if (SmallButton("x")) remove_net = net;
            SetItemTooltip("Remove signal");
            text_x = GetItemRectMax().x + 5;
            if (run.sim().net_width(net) > 1) {
                SameLine(0, 2);
                if (ArrowButton("expand", settings.expanded ? ImGuiDir_Down : ImGuiDir_Right)) settings.expanded = !settings.expanded;
                SetItemTooltip(settings.expanded ? "Collapse bus" : "Expand bus into individual bits");
                text_x = GetItemRectMax().x + 5;
            }
            PopStyleVar();
        }
        std::string name = bit < 0 ? waveform_signal_name(signal.name, full_path) : "[" + std::to_string(bit) + "]";
        SetCursorScreenPos({text_x, top});
        InvisibleButton("signal_name", {std::max(1.0f, origin.x + name_width - text_x - 4), row_height});
        if (IsItemHovered()) SetTooltip("%s%s\n%s", signal.name.c_str(), bit >= 0 ? name.c_str() : "", format_names[int(selected_format)]);
        if (bit < 0 && BeginPopupContextItem("signal_options")) {
            TextDisabled("%s", signal.name.c_str());
            SeparatorText("Display format");
            if (MenuItem("Use default", nullptr, !settings.format)) settings.format.reset();
            for (int f = 0; f < IM_ARRAYSIZE(format_names); f++)
                if (MenuItem(format_names[f], nullptr, settings.format == waveform_format(f))) settings.format = waveform_format(f);
            if (run.sim().net_width(net) > 1) {
                Separator();
                MenuItem("Expand bits", nullptr, &settings.expanded);
            }
            EndPopup();
        }
        draw->PushClipRect({text_x, top}, {origin.x + name_width - 4, top + row_height}, true);
        draw->AddText({text_x, top + 2}, text_color, name.c_str());
        std::string value_text = "No data";
        auto inspected = std::find_if(values.signals.begin(), values.signals.end(), [net](auto& v) { return v.net == net; });
        if (inspected != values.signals.end() && !inspected->samples.empty() &&
            inspected->samples.front().at <= inspect_at && inspect_at <= values.now) {
            auto& v = inspected->samples.back().v;
            value_text = bit >= 0 ? sim::to_string(v.slice(bit, 1)) : format_waveform_value(v, selected_format);
        }
        draw->AddText({text_x, top + GetTextLineHeightWithSpacing()}, muted, value_text.c_str());
        draw->PopClipRect();
        PopID();
        PopID();

        draw_trace(*draw, signal, bit, selected_format, start, span, data.now, left, width, top, row_height, hovered);
    }
    PopClipRect();
    draw->AddRectFilled({left - divider, axis_y}, {left, axis_y + visible_height},
        GetColorU32(resizing ? ImGuiCol_SeparatorActive : resize_hovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator));
    if (has_cursor && cursor >= as_time(start) && cursor <= as_time(start + span))
        draw->AddLine({x_of(cursor), axis_y}, {x_of(cursor), origin.y + height}, IM_COL32(240, 200, 70, 255));
    SetCursorScreenPos({origin.x, origin.y + height});
    Dummy({1, 1});
    EndChild();
    if (remove_net >= 0) {
        options.erase(remove_net);
        run.post([remove_net](sim::simulator& s) { s.waveforms.remove(remove_net); });
    }
    End();
}

}
