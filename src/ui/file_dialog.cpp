#include "file_dialog.h"
#include "file_list.h"
#include "icons.h"
#include "theme.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <vector>

namespace logic::ui {

namespace fs = std::filesystem;

const char* const firmware_files = "Firmware and memory contents{.hex,.ihx,.ihex,.srec,.s19,.s28,.s37,.mot,.elf,.bin},.*";
const char* const hdl_files = "HDL sources{.v,.sv,.vh,.svh,.vhd,.vhdl},.*";
#if defined(_WIN32)
const char* const plugin_files = "Plugins{.dll},.*";
#else
const char* const plugin_files = "Plugins{.so},.*";
#endif
const char* const waveform_files = "Value change dumps{.vcd},.*";
const char* const session_files = "Waveform sessions{.json},.*";

namespace {

struct file_filter {
    std::string label;
    // lower case with the dot, all files if empty
    std::vector<std::string> extensions;
};

struct dialog {
    std::string title;
    bool save = false;
    std::vector<file_filter> filters;
    int filter = 0;
    file_list list;
    // the file name typed or chosen in the list
    std::string name;
    // name was changed here, the text field takes it even while it's edited
    bool name_set = false;
    std::string error;
    // OpenPopup at the next draw, in the caller's id stack
    bool pending = false;
    // drawn this frame
    bool visible = false;
    // asking before overwriting it
    fs::path replacing;
};

std::map<std::string, dialog> dialogs;
fs::path dialog_root;
std::function<void(const fs::path&)> upload;

std::string lower(std::string s)
{
    for (auto& c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

// "Description{.a,.b},.*" into its filters
std::vector<file_filter> parse_filters(const std::string& s)
{
    std::vector<file_filter> r;
    size_t k = 0;
    while (k < s.size()) {
        auto brace = s.find('{', k);
        auto comma = s.find(',', k);
        file_filter f;
        if (brace != std::string::npos && (comma == std::string::npos || brace < comma)) {
            auto close = s.find('}', brace);
            if (close == std::string::npos) close = s.size();
            f.label = s.substr(k, brace - k);
            std::string list = s.substr(brace + 1, close - brace - 1);
            for (size_t i = 0; i <= list.size();) {
                auto j = std::min(list.find(',', i), list.size());
                if (j > i) f.extensions.push_back(lower(list.substr(i, j - i)));
                i = j + 1;
            }
            k = close + 1;
            if (k < s.size() && s[k] == ',') k++;
        } else {
            auto end = comma == std::string::npos ? s.size() : comma;
            auto e = s.substr(k, end - k);
            if (e == ".*") f.label = "All files";
            else {
                f.label = e;
                f.extensions.push_back(lower(e));
            }
            k = end + 1;
        }
        // the extensions in the label
        if (!f.extensions.empty() && f.label != f.extensions[0]) {
            f.label += " (";
            for (size_t i = 0; i < f.extensions.size(); i++)
                f.label += (i ? " " : "") + f.extensions[i];
            f.label += ")";
        }
        r.push_back(std::move(f));
    }
    if (r.empty()) r.push_back({"All files", {}});
    return r;
}

bool matches(const file_filter& f, const fs::path& p)
{
    if (f.extensions.empty()) return true;
    auto name = lower(p.filename().string());
    return std::any_of(f.extensions.begin(), f.extensions.end(),
        [&](auto& e) { return name.size() > e.size() && name.compare(name.size() - e.size(), e.size(), e) == 0; });
}

bool inside(const fs::path& p, const fs::path& root)
{
    if (root.empty()) return true;
    auto rel = p.lexically_normal().lexically_relative(root.lexically_normal());
    return !rel.empty() && *rel.begin() != "..";
}

fs::path home()
{
    if (!dialog_root.empty()) return dialog_root;
#ifdef _WIN32
    auto h = std::getenv("USERPROFILE");
#else
    auto h = std::getenv("HOME");
#endif
    std::error_code ec;
    return h ? fs::path(h) : fs::current_path(ec);
}

// p relative to base if it's inside it
std::string relative_if_inside(const fs::path& p, const fs::path& base)
{
    if (base.empty()) return p.generic_string();
    std::error_code ec;
    auto rel = fs::relative(p, base, ec);
    if (ec || rel.empty() || *rel.begin() == "..") return p.generic_string();
    return rel.generic_string();
}

// sets up the dialog of key, shown at the next display(key)
void start(const std::string& key, const char* title, const char* filters, fs::path dir, const std::string& name, bool save)
{
    auto& d = dialogs[key];
    d = dialog{};
    d.title = title;
    d.save = save;
    d.filters = parse_filters(filters ? filters : ".*");
    d.list.root = dialog_root;
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec) || !inside(dir, dialog_root)) dir = home();
    d.list.go(dir, name.empty() ? fs::path{} : dir / name);
    d.list.show = [&d](const fs::path& p) { return matches(d.filters[d.filter], p); };
    d.name = name;
    d.pending = true;
}

// the typed name as a path, relative to the folder shown
fs::path typed(const dialog& d)
{
    fs::path p = d.name;
    if (p.is_relative()) p = d.list.dir / p;
    return p.lexically_normal();
}

// what's typed or chosen: true with file if it's the result, else goes into
// a folder, asks before overwriting or sets an error
bool accept(dialog& d, fs::path& file)
{
    d.error.clear();
    if (d.name.empty()) {
        d.error = d.save ? "Type a name for the file" : "Choose a file";
        return false;
    }
    auto p = typed(d);
    std::error_code ec;
    if (!inside(p, dialog_root)) {
        d.error = "That's outside of " + dialog_root.string();
        return false;
    }
    if (fs::is_directory(p, ec)) {
        d.list.go(p);
        d.name.clear();
        d.name_set = true;
        return false;
    }
    if (!d.save) {
        if (!fs::is_regular_file(p, ec)) {
            d.error = "'" + d.name + "' isn't there";
            return false;
        }
        file = p;
        return true;
    }
    auto& f = d.filters[d.filter];
    if (!p.has_extension() && !f.extensions.empty()) p += f.extensions[0];
    if (!fs::is_directory(p.parent_path(), ec)) {
        d.error = "There's no folder " + p.parent_path().string();
        return false;
    }
    if (fs::exists(p, ec)) {
        d.replacing = p;
        return false;
    }
    file = p;
    return true;
}

// draws the dialog of key, true once a file was chosen
bool display(const std::string& key, fs::path& file)
{
    using namespace ImGui;

    auto it = dialogs.find(key);
    if (it == dialogs.end()) return false;
    auto& d = it->second;
    auto popup = d.title + "###" + key;
    if (d.pending) {
        OpenPopup(popup.c_str());
        d.pending = false;
    }
    auto& vp = *GetMainViewport();
    SetNextWindowSize({vp.Size.x * 0.6f, vp.Size.y * 0.6f}, ImGuiCond_Appearing);
    bool open = true;
    d.visible = BeginPopupModal(popup.c_str(), &open, ImGuiWindowFlags_NoCollapse);
    if (!d.visible) {
        dialogs.erase(it);
        return false;
    }

    bool chosen = false;
    auto old_selection = d.list.selected;

    if (Button(icon::home)) d.list.go(home());
    SetItemTooltip(dialog_root.empty() ? "Your home folder" : "The workspace");
    SameLine();
#ifdef _WIN32
    // the drives
    SetNextItemWidth(GetFontSize() * 4.0f);
    auto drive = d.list.dir.root_name().string();
    if (BeginCombo("##drive", drive.c_str())) {
        for (char c = 'A'; c <= 'Z'; c++) {
            fs::path p = std::string(1, c) + ":\\";
            std::error_code ec;
            if (fs::exists(p, ec) && Selectable(p.string().c_str(), p.root_name().string() == drive)) d.list.go(p);
        }
        EndCombo();
    }
    SameLine();
#endif
    if (upload) {
        if (Button(icon::upload)) upload(d.list.dir);
        SetItemTooltip("Upload files from your computer into this folder, replacing ones with the same names");
        SameLine();
    }
    d.list.path_bar();

    // the list over the name and the buttons
    auto& style = GetStyle();
    float footer = GetFrameHeightWithSpacing() + (d.error.empty() ? 0.0f : GetTextLineHeightWithSpacing());
    fs::path activated;
    if (d.list.table(activated, -footer)) {
        d.name = activated.filename().string();
        chosen = accept(d, file);
    }
    // a chosen file goes into the name
    std::error_code ec;
    if (d.list.selected != old_selection && !d.list.selected.empty() && fs::is_regular_file(d.list.selected, ec)) {
        d.name = d.list.selected.filename().string();
        d.name_set = true;
    }

    if (!d.error.empty()) TextColored(color(color_id::error), "%s", d.error.c_str());

    const char* ok = d.save ? "Save" : "Open";
    float buttons = CalcTextSize(ok).x + CalcTextSize("Cancel").x + style.FramePadding.x * 4.0f + style.ItemSpacing.x;
    float combo = std::min(GetContentRegionAvail().x * 0.35f, GetFontSize() * 18.0f);
    SetNextItemWidth(GetContentRegionAvail().x - combo - buttons - style.ItemSpacing.x * 2.0f);
    if (IsWindowAppearing()) SetKeyboardFocusHere();
    if (d.name_set) {
        if (auto state = GetInputTextState(GetID("##name"))) state->ReloadUserBufAndSelectAll();
        d.name_set = false;
    }
    char buf[1024];
    d.name.copy(buf, sizeof(buf) - 1);
    buf[std::min(d.name.size(), sizeof(buf) - 1)] = 0;
    if (InputTextWithHint("##name", d.save ? "file name" : "file name or path", buf, sizeof(buf))) d.name = buf;
    bool enter = IsItemDeactivated() && (IsKeyPressed(ImGuiKey_Enter) || IsKeyPressed(ImGuiKey_KeypadEnter));
    SameLine();
    SetNextItemWidth(combo);
    if (BeginCombo("##filter", d.filters[d.filter].label.c_str())) {
        for (int k = 0; k < (int)d.filters.size(); k++)
            if (Selectable(d.filters[k].label.c_str(), k == d.filter)) {
                d.filter = k;
                d.list.refresh();
            }
        EndCombo();
    }
    SameLine();
    if ((Button(ok) || enter) && !chosen) chosen = accept(d, file);
    SameLine();
    if (Button("Cancel") || (IsKeyPressed(ImGuiKey_Escape) && d.replacing.empty())) open = false;

    // asked before saving over a file
    if (!d.replacing.empty()) OpenPopup("Replace?");
    if (BeginPopupModal("Replace?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        Text("'%s' is already there. Replace it?", d.replacing.filename().string().c_str());
        if (Button("Replace")) {
            file = d.replacing;
            chosen = true;
            d.replacing.clear();
            CloseCurrentPopup();
        }
        SameLine();
        if (Button("Cancel") || IsKeyPressed(ImGuiKey_Escape)) {
            d.replacing.clear();
            CloseCurrentPopup();
        }
        EndPopup();
    }

    if (chosen || !open) CloseCurrentPopup();
    EndPopup();
    if (chosen || !open) dialogs.erase(key);
    return chosen;
}

}

bool browse_button(const char* id, std::string& path, const fs::path& base, const char* filters, const char* title)
{
    using namespace ImGui;

    PushID(id);
    // one dialog per button
    auto key = "file_dialog_" + std::to_string(GetID("dialog"));

    if (Button("...")) {
        std::error_code ec;
        fs::path current = path.empty() ? fs::path{} : fs::path(path);
        if (!current.empty() && current.is_relative()) current = base / current;
        fs::path dir = current.empty() ? base : current.parent_path();
        if (dir.empty() || !fs::is_directory(dir, ec)) dir = base;
        start(key, title, filters, dir, current.filename().string(), false);
    }
    SetItemTooltip("Choose a file");

    fs::path file;
    bool chosen = display(key, file);
    if (chosen) path = relative_if_inside(file, base);
    PopID();
    return chosen;
}

void open_dialog(const char* id, const char* title, const char* filters, const fs::path& start_at, bool save)
{
    std::error_code ec;
    bool is_dir = fs::is_directory(start_at, ec);
    start(id, title, filters, is_dir ? start_at : start_at.parent_path(), is_dir ? std::string{} : start_at.filename().string(), save);
}

bool dialog_result(const char* id, fs::path& file)
{
    return display(id, file);
}

void set_dialog_root(const fs::path& root)
{
    dialog_root = root;
}

void set_dialog_upload(std::function<void(const fs::path&)> u)
{
    upload = std::move(u);
}

fs::path dialog_folder()
{
    for (auto& [key, d] : dialogs)
        if (d.visible) return d.list.dir;
    return {};
}

bool dialog_show_file(const fs::path& file)
{
    for (auto& [key, d] : dialogs) {
        if (!d.visible) continue;
        // all files if the filter would hide it
        if (!matches(d.filters[d.filter], file))
            for (int k = 0; k < (int)d.filters.size(); k++)
                if (d.filters[k].extensions.empty()) d.filter = k;
        d.list.go(file.parent_path(), file);
        d.name = file.filename().string();
        d.name_set = true;
        d.error.clear();
        return true;
    }
    return false;
}

}
