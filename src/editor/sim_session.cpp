#include "sim_session.h"
#include "../ui/file_dialog.h"
#include "../ui/fonts.h"
#include "../ui/icons.h"
#include "../ui/zoom.h"
#include "../wave/search.h"
#include "../wave/vcd.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <fstream>

namespace logic::editor {

sim_session::sim_session(const model::workspace& ws, const model::cell& c, const sim::component_factory& factory)
    : run(std::make_unique<sim::simulator>(ws, c, factory))
    , top(c.id)
{
    for (auto& d : run.sim().displays()) {
        display_window w;
        w.d = &d;
        displays.push_back(std::move(w));
    }
    for (auto& t : run.sim().terminals()) {
        terminal_window w;
        w.t = &t;
        w.vt = std::make_unique<ui::vt100>();
        terminals.push_back(std::move(w));
    }
}

bool sim_session::has_display(int component) const
{
    return std::any_of(displays.begin(), displays.end(), [&](auto& w) { return w.d->component == component; });
}

bool sim_session::show_display(int component)
{
    for (auto& w : displays) {
        if (w.d->component != component) continue;
        w.open = true;
        ImGui::SetWindowFocus(("###display" + std::to_string(top) + "_" + std::to_string(component)).c_str());
        return true;
    }
    return false;
}

void sim_session::displays_gui()
{
    for (auto& w : displays)
        if (w.open) display_gui(w);
}

void sim_session::display_gui(display_window& w)
{
    using namespace ImGui;

    auto& buf = *w.d->pixels;
    auto label = "Framebuffer: " + w.d->path + "###display" + std::to_string(top) + "_" + std::to_string(w.d->component);
    SetNextWindowSize({std::clamp(buf.width * 4.0f, 240.0f, 800.0f) + 20, std::clamp(buf.height * 4.0f, 180.0f, 600.0f) + 60}, ImGuiCond_FirstUseEver);
    SetNextWindowPos(GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    if (!ui::zoom::begin(label.c_str(), &w.open, ImGuiWindowFlags_NoCollapse)) {
        ui::zoom::end();
        return;
    }

    if (!w.tex) {
        w.tex.emplace(Hd::Texture::emptyTexture(buf.width, buf.height));
        // for backends without samplers, see the callbacks around Image
        glBindTexture(GL_TEXTURE_2D, w.tex->Id());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    bool changed = false;
    {
        std::lock_guard lk(buf.m);
        if (w.version != buf.version) {
            w.pixels = buf.rgba;
            w.version = buf.version;
            changed = true;
        }
    }
    if (changed) {
        glBindTexture(GL_TEXTURE_2D, w.tex->Id());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, buf.width, buf.height, GL_RGBA, GL_UNSIGNED_BYTE, w.pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    static const char* const zooms[] = {"Fit", "1x", "2x", "4x", "8x"};
    static const int factors[] = {0, 1, 2, 4, 8};
    int pick = 0;
    for (int k = 0; k < 5; k++)
        if (factors[k] == w.zoom) pick = k;
    SetNextItemWidth(GetFontSize() * 5);
    if (Combo("Zoom", &pick, zooms, 5)) w.zoom = factors[pick];
    SameLine();
    TextDisabled("%d x %d", buf.width, buf.height);

    BeginChild("picture", {0, 0}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    auto avail = GetContentRegionAvail();
    float scale = (float)w.zoom;
    // as large as fits, keeping the aspect ratio
    if (w.zoom == 0) scale = std::max(std::min(avail.x / buf.width, avail.y / buf.height), 0.01f);
    ImVec2 size{buf.width * scale, buf.height * scale};
    // centered while it fits
    auto at = GetCursorPos();
    SetCursorPos({at.x + std::max(0.0f, (avail.x - size.x) / 2), at.y + std::max(0.0f, (avail.y - size.y) / 2)});
    auto origin = GetCursorScreenPos();
    // imgui samples linearly, which would blur the pixels
    auto& pio = GetPlatformIO();
    if (pio.DrawCallback_SetSamplerNearest) GetWindowDrawList()->AddCallback(pio.DrawCallback_SetSamplerNearest, nullptr);
    Image((ImTextureID)w.tex->Id(), size);
    if (pio.DrawCallback_SetSamplerLinear) GetWindowDrawList()->AddCallback(pio.DrawCallback_SetSamplerLinear, nullptr);
    if (IsItemHovered() && !w.pixels.empty()) {
        auto m = GetMousePos();
        int x = std::clamp(int((m.x - origin.x) / scale), 0, buf.width - 1);
        int y = std::clamp(int((m.y - origin.y) / scale), 0, buf.height - 1);
        uint32_t c = w.pixels[size_t(y) * buf.width + x];
        SetTooltip("x %d, y %d: #%02X%02X%02X", x, y, c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
    }
    EndChild();
    ui::zoom::end();
}

bool sim_session::has_terminal(int component) const
{
    return std::any_of(terminals.begin(), terminals.end(), [&](auto& w) { return w.t->component == component; });
}

bool sim_session::show_terminal(int component)
{
    for (auto& w : terminals) {
        if (w.t->component != component) continue;
        w.open = true;
        ImGui::SetWindowFocus(("###terminal" + std::to_string(top) + "_" + std::to_string(component)).c_str());
        return true;
    }
    return false;
}

void sim_session::send(const terminal_window& w, const std::string& bytes)
{
    if (bytes.empty()) return;
    run.post([k = w.t->component, bytes](sim::simulator& sim) {
        for (char c : bytes)
            sim.interact(k, "key", (uint8_t)c);
    });
}

void sim_session::terminals_gui()
{
    for (auto& w : terminals) {
        std::string bytes;
        bool reset = false;
        {
            auto& st = *w.t->stream;
            std::lock_guard lk(st.m);
            bytes.swap(st.bytes);
            if (st.resets != w.resets) {
                w.resets = st.resets;
                reset = true;
            }
        }
        if (reset) {
            w.vt->reset();
            w.sel_from.reset();
        }
        if (!bytes.empty()) w.vt->feed(bytes);
        // answers to the program's queries, e.g. the cursor position
        send(w, w.vt->take_reply());
        if (w.open) terminal_gui(w);
    }
}

namespace {

// the bytes a key sends, empty for keys that send nothing
std::string key_bytes(ImGuiKey k, bool app_cursor)
{
    auto cursor = [&](char c) { return std::string(app_cursor ? "\x1bO" : "\x1b[") + c; };
    switch (k) {
    case ImGuiKey_Enter:
    case ImGuiKey_KeypadEnter: return "\r";
    case ImGuiKey_Backspace: return "\x7f";
    case ImGuiKey_Tab: return "\t";
    case ImGuiKey_Escape: return "\x1b";
    case ImGuiKey_UpArrow: return cursor('A');
    case ImGuiKey_DownArrow: return cursor('B');
    case ImGuiKey_RightArrow: return cursor('C');
    case ImGuiKey_LeftArrow: return cursor('D');
    case ImGuiKey_Home: return cursor('H');
    case ImGuiKey_End: return cursor('F');
    case ImGuiKey_Insert: return "\x1b[2~";
    case ImGuiKey_Delete: return "\x1b[3~";
    case ImGuiKey_PageUp: return "\x1b[5~";
    case ImGuiKey_PageDown: return "\x1b[6~";
    case ImGuiKey_F1: return "\x1bOP";
    case ImGuiKey_F2: return "\x1bOQ";
    case ImGuiKey_F3: return "\x1bOR";
    case ImGuiKey_F4: return "\x1bOS";
    case ImGuiKey_F5: return "\x1b[15~";
    case ImGuiKey_F6: return "\x1b[17~";
    case ImGuiKey_F7: return "\x1b[18~";
    case ImGuiKey_F8: return "\x1b[19~";
    case ImGuiKey_F9: return "\x1b[20~";
    case ImGuiKey_F10: return "\x1b[21~";
    case ImGuiKey_F11: return "\x1b[23~";
    case ImGuiKey_F12: return "\x1b[24~";
    default: return {};
    }
}

void append_utf8(std::string& s, unsigned c)
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

ImU32 rgb(ui::vt100::color c) { return IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255); }

}

void sim_session::terminal_gui(terminal_window& w)
{
    using namespace ImGui;
    auto& vt = *w.vt;

    auto label = "Terminal: " + w.t->path + "###terminal" + std::to_string(top) + "_" + std::to_string(w.t->component);
    float char_w, line_h;
    {
        ui::mono_scope mono;
        char_w = CalcTextSize("M").x;
        line_h = GetTextLineHeight();
    }
    SetNextWindowSize({char_w * 80 + 40, line_h * 24 + GetFrameHeightWithSpacing() * 2 + 30}, ImGuiCond_FirstUseEver);
    SetNextWindowPos(GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    if (!ui::zoom::begin(label.c_str(), &w.open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNavInputs)) {
        ui::zoom::end();
        return;
    }

    auto selection_text = [&]() -> std::string {
        if (!w.sel_from || !w.sel_to) return {};
        auto a = *w.sel_from, b = *w.sel_to;
        if (b < a) std::swap(a, b);
        return vt.text(a.first, a.second, b.first, b.second);
    };
    auto paste = [&]() {
        const char* clip = GetClipboardText();
        if (!clip) return;
        std::string bytes;
        for (const char* p = clip; *p; p++)
            if (*p != '\r') bytes += *p == '\n' ? '\r' : *p;
        send(w, bytes);
        w.follow = true;
    };

    if (Button((std::string(ui::icon::paste) + " Paste").c_str())) paste();
    SetItemTooltip("Types the clipboard's text (also Shift+Insert or Ctrl+Shift+V)");
    SameLine();
    BeginDisabled(!w.sel_from);
    if (Button((std::string(ui::icon::copy) + " Copy").c_str())) SetClipboardText(selection_text().c_str());
    SetItemTooltip("Copies the selection (also Ctrl+Shift+C)");
    EndDisabled();
    SameLine();
    if (Button((std::string(ui::icon::eraser) + " Clear").c_str())) {
        vt.reset();
        w.sel_from.reset();
    }
    SetItemTooltip("Clears the screen and its history here, the program doesn't see it");
    SameLine();
    Checkbox("Fit window", &w.fit);
    SetItemTooltip("The screen's size follows the window, else 80 x 24. Programs don't learn the size over a serial line, tell them with stty rows and cols.");
    SameLine();
    TextDisabled("%d x %d", vt.cols(), vt.rows());

    BeginChild("screen", {0, 0}, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_HorizontalScrollbar);
    // popped before EndChild
    std::optional<ui::mono_scope> mono;
    mono.emplace();
    if (w.fit) {
        auto avail = GetContentRegionAvail();
        vt.resize(std::max(2, int(avail.x / char_w)), std::max(2, int(avail.y / line_h)));
    } else if (vt.cols() != 80 || vt.rows() != 24) {
        vt.resize(80, 24);
    }

    bool focused = IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImGuiID owner = GetID("keys");
    auto& io = GetIO();
    if (focused) {
        // the keys are the terminal's while it has focus, not the app's
        // shortcuts'
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_GamepadStart; k++)
            SetKeyOwner((ImGuiKey)k, owner, ImGuiInputFlags_LockUntilRelease);
        std::string bytes;
        bool ctrl = io.KeyCtrl && !io.KeyAlt;
        if (!ctrl)
            for (auto c : io.InputQueueCharacters)
                append_utf8(bytes, c);
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_GamepadStart; k++) {
            auto key = (ImGuiKey)k;
            if (!IsKeyPressed(key, ImGuiInputFlags_Repeat, owner)) continue;
            if (io.KeyShift && (key == ImGuiKey_PageUp || key == ImGuiKey_PageDown)) {
                SetScrollY(GetScrollY() + (key == ImGuiKey_PageUp ? -1 : 1) * line_h * float(vt.rows() - 1));
                w.follow = false;
                continue;
            }
            if ((io.KeyShift && key == ImGuiKey_Insert) || (ctrl && io.KeyShift && key == ImGuiKey_V)) {
                paste();
                continue;
            }
            if (ctrl && io.KeyShift && key == ImGuiKey_C) {
                SetClipboardText(selection_text().c_str());
                continue;
            }
            if (ctrl && key >= ImGuiKey_A && key <= ImGuiKey_Z) {
                bytes += char(1 + (key - ImGuiKey_A));
                continue;
            }
            if (ctrl && (key == ImGuiKey_LeftBracket || key == ImGuiKey_Backslash || key == ImGuiKey_RightBracket || key == ImGuiKey_Space)) {
                bytes += key == ImGuiKey_LeftBracket ? '\x1b' : key == ImGuiKey_Backslash ? '\x1c' : key == ImGuiKey_RightBracket ? '\x1d' : '\0';
                continue;
            }
            bytes += key_bytes(key, vt.application_cursor());
        }
        if (!bytes.empty()) {
            send(w, bytes);
            w.follow = true;
        }
    }

    // the lines of the history and the screen, drawn as runs of one style
    size_t lines = vt.lines();
    auto origin = GetCursorScreenPos();
    float width = char_w * float(vt.cols());
    Dummy({width, line_h * float(lines)});
    auto* dl = GetWindowDrawList();
    ImU32 default_fg = GetColorU32(ImGuiCol_Text);
    // opaque, the window's background may be translucent
    ImU32 default_bg = GetColorU32(ImGuiCol_WindowBg) | IM_COL32_A_MASK;
    ImU32 selection = GetColorU32(ImGuiCol_TextSelectedBg);
    auto sel_a = w.sel_from, sel_b = w.sel_to;
    if (sel_a && sel_b && *sel_b < *sel_a) std::swap(sel_a, sel_b);
    auto selected = [&](size_t l, int c) {
        if (!sel_a || !sel_b) return false;
        auto p = std::make_pair(l, c);
        return !(p < *sel_a) && !(*sel_b < p);
    };
    float scroll = GetScrollY(), view_h = GetWindowHeight();
    size_t first = size_t(std::max(0.0f, scroll / line_h)), last = std::min(lines, size_t((scroll + view_h) / line_h) + 2);
    dl->AddRectFilled(origin, {origin.x + width, origin.y + line_h * float(lines)}, default_bg);
    for (size_t l = first; l < last; l++) {
        auto& row = vt.line_at(l);
        float y = origin.y + line_h * float(l);
        for (int c = 0; c < (int)row.size();) {
            auto& cell = row[size_t(c)];
            bool sel = selected(l, c);
            // a run of cells with the same style
            int end = c + 1;
            while (end < (int)row.size() && row[size_t(end)].fg == cell.fg && row[size_t(end)].bg == cell.bg && row[size_t(end)].attrs == cell.attrs &&
                selected(l, end) == sel)
                end++;
            ImU32 fg = cell.fg ? rgb(cell.fg) : default_fg, bg = cell.bg ? rgb(cell.bg) : default_bg;
            if (cell.attrs & ui::vt100::inverse) std::swap(fg, bg);
            if (sel) bg = selection;
            if (cell.attrs & ui::vt100::dim) fg = (fg & 0x00FFFFFF) | 0x99000000;
            float x0 = origin.x + char_w * float(c), x1 = origin.x + char_w * float(end);
            if (bg != default_bg) dl->AddRectFilled({x0, y}, {x1, y + line_h}, bg);
            std::string text;
            for (int k = c; k < end; k++)
                append_utf8(text, row[size_t(k)].ch);
            dl->AddText({x0, y}, fg, text.c_str());
            if (cell.attrs & ui::vt100::bold) dl->AddText({x0 + 1, y}, fg, text.c_str());
            if (cell.attrs & ui::vt100::underline) dl->AddLine({x0, y + line_h - 1}, {x1, y + line_h - 1}, fg);
            c = end;
        }
    }
    // the cursor, a block while focused
    if (vt.cursor_visible()) {
        float x = origin.x + char_w * float(vt.cursor_col()), y = origin.y + line_h * float(vt.history_size() + size_t(vt.cursor_row()));
        if (focused)
            dl->AddRectFilled({x, y}, {x + char_w, y + line_h}, (default_fg & 0x00FFFFFF) | 0x90000000);
        else
            dl->AddRect({x, y}, {x + char_w, y + line_h}, default_fg);
    }

    // selecting with the mouse
    auto cell_at = [&](ImVec2 m) {
        long l = long((m.y - origin.y) / line_h);
        int c = int((m.x - origin.x) / char_w);
        l = std::clamp(l, 0L, long(lines) - 1);
        return std::make_pair(size_t(l), std::clamp(c, 0, vt.cols() - 1));
    };
    if (IsWindowHovered() && IsMouseClicked(ImGuiMouseButton_Left)) {
        w.sel_from = w.sel_to = cell_at(GetMousePos());
        w.selecting = true;
    }
    if (w.selecting) {
        if (IsMouseDown(ImGuiMouseButton_Left)) {
            w.sel_to = cell_at(GetMousePos());
        } else {
            w.selecting = false;
            // a click without dragging selects nothing
            if (w.sel_from == w.sel_to) w.sel_from.reset(), w.sel_to.reset();
        }
    }
    if (BeginPopupContextWindow("terminal menu")) {
        if (MenuItem("Copy", "Ctrl+Shift+C", false, w.sel_from.has_value())) SetClipboardText(selection_text().c_str());
        if (MenuItem("Copy all")) SetClipboardText(vt.text(0, lines - 1).c_str());
        if (MenuItem("Paste", "Shift+Insert")) paste();
        EndPopup();
    }

    // following the output unless scrolled up
    if (GetIO().MouseWheel != 0 && IsWindowHovered()) w.follow = false;
    if (GetScrollY() >= GetScrollMaxY() - 1) w.follow = true;
    if (w.follow) SetScrollY(GetScrollMaxY());
    mono.reset();
    EndChild();
    ui::zoom::end();
}

void sim_session::watch(const sim::scope* s, int instance)
{
    watch_scope = s;
    watch_instance = instance;
    std::lock_guard lk(watch_state->m);
    watch_state->data = {};
}

void sim_session::refresh_watch(int memory, uint64_t address, size_t bytes, std::optional<uint64_t> from, int lines)
{
    if (!watch_scope || watch_instance < 0) return;
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard lk(watch_state->m);
        // one request at a time, not too often while running
        if (watch_state->pending) return;
        if (run.running() && now - last_refresh < std::chrono::milliseconds(100)) return;
        watch_state->pending = true;
    }
    last_refresh = now;

    run.query([state = watch_state, s = watch_scope, k = watch_instance, memory, address, bytes, from, lines](sim::simulator& sim) {
        watch_data d;
        d.memory = memory;
        d.memory_address = address;
        if (auto c = sim.component_of(*s, k)) {
            d.valid = true;
            c->inspect(d.items);
            if (auto dbg = c->debug()) {
                d.debug = true;
                d.memories = dbg->memories();
                d.pc = dbg->pc();
                d.breakpoints = dbg->breakpoints();
                if (memory >= 0 && memory < (int)d.memories.size() && address < d.memories[memory].size) {
                    d.bytes.resize(std::min<uint64_t>(bytes, d.memories[memory].size - address));
                    if (!dbg->read_memory(memory, address, d.bytes)) d.bytes.clear();
                }
                auto at = from ? from : d.pc;
                for (int n = 0; at && n < lines; n++) {
                    std::string text;
                    int len = dbg->disassemble(*at, text);
                    if (len <= 0) break;
                    d.disassembly.push_back({*at, len, text});
                    *at += len;
                }
            }
        }
        std::lock_guard lk(state->m);
        state->data = std::move(d);
        state->pending = false;
    });
}

watch_data sim_session::watched() const
{
    std::lock_guard lk(watch_state->m);
    return watch_state->data;
}

void sim_session::debug(std::function<void(sim::debug_interface&)> f)
{
    if (!watch_scope || watch_instance < 0) return;
    run.post([s = watch_scope, k = watch_instance, f = std::move(f)](sim::simulator& sim) {
        if (auto c = sim.component_of(*s, k))
            if (auto d = c->debug()) f(*d);
    });
}

void sim_session::interact(const sim::scope* s, int instance, std::string action, int64_t value)
{
    run.post([s, instance, action = std::move(action), value](sim::simulator& sim) { sim.interact(*s, instance, action, value); });
}

bool sim_session::uses(model::id cell) const
{
    auto& c = run.sim().cells();
    return std::find(c.begin(), c.end(), cell) != c.end();
}


namespace {

// run controls and export in the view's toolbar, rows for every recorded
// signal
class recording_host : public wave::view_host {
public:
    recording_host(sim::runner& run, std::string dialog, std::string name)
        : run(run)
        , dialog(std::move(dialog))
        , name(std::move(name))
    {
    }

    void toolbar() override
    {
        using namespace ImGui;
        if (ui::small_icon_button(run.running() ? ui::icon::pause : ui::icon::play, run.running() ? "Pause" : "Run")) run.run(!run.running());
        SameLine(0, 2);
        if (ui::small_icon_button(ui::icon::step, "Step")) run.step();
        SameLine(0, 2);
        if (ui::small_icon_button(ui::icon::eraser, "Clear: start the history over from now")) run.query([](sim::simulator& s) { s.waveforms.clear(s); });
        SameLine(0, 2);
        if (ui::small_icon_button(ui::icon::download, "Export VCD..."))
            ui::open_dialog(dialog.c_str(), "Export waveforms", ui::waveform_files, std::filesystem::current_path() / (name + ".vcd"), true);
        SameLine(0, 16);
    }
    void removed(int64_t key) override
    {
        run.query([key](sim::simulator& s) { s.waveforms.remove(key); });
    }
    bool rows_follow_signals() const override { return true; }
    bool live() const override { return true; }
    bool running() const override { return run.running(); }

private:
    sim::runner& run;
    std::string dialog;
    std::string name;
};

}

void sim_session::waveforms_gui(const std::string& title)
{
    using namespace ImGui;
    if (!waveforms_visible) return;
    run.sim().waveforms.sync(wave_data, wave_sync);

    SetNextWindowSize({1000, 460}, ImGuiCond_FirstUseEver);
    auto id = std::to_string(top);
    auto label = "Waveforms: " + title + "###waveforms" + id;
    // double-clicking the title zooms it instead of collapsing
    if (!ui::zoom::begin(label.c_str(), &waveforms_visible, ImGuiWindowFlags_NoCollapse)) {
        ui::zoom::end();
        return;
    }
    auto dialog = "export_vcd_" + id;
    recording_host host(run, dialog, title);
    if (!wave_message.empty()) {
        TextDisabled("%s", wave_message.c_str());
        SameLine();
        if (SmallButton("OK")) wave_message.clear();
    }
    waves.draw(wave_data, host);

    std::filesystem::path file;
    if (ui::dialog_result(dialog.c_str(), file)) {
        std::ofstream out(file);
        // a bundle's fields as signals of their own, in a scope
        wave::write_vcd(out, wave::expand_fields(wave_data));
        bool truncated = false;
        for (auto& t : wave_data.traces)
            truncated |= !t.empty() && t.at(0) > wave_data.begin;
        if (!out) wave_message = "Couldn't write " + file.string();
        else
            wave_message = "Exported to " + file.string() +
                (truncated ? ". Only the newest " + std::to_string(sim::waveform_recorder::max_samples) + " changes of each signal are kept." : "");
    }
    ui::zoom::end();
}

}
