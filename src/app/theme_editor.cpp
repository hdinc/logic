#include "theme_editor.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace logic::app {

namespace {

const char* const groups[] = {"Canvas", "Signals", "Waveforms", "Messages", "Displays", ui::interface_group};

bool same(ImVec4 a, ImVec4 b)
{
    return ui::to_hex(a) == ui::to_hex(b);
}

}

bool theme_editor::check_name(const std::string& name)
{
    if (name.empty()) {
        error = "Give the theme a name";
        return false;
    }
    if (name.find_first_of("/\\:*?\"<>|") != std::string::npos || name[0] == '.') {
        error = "A name can't contain / \\ : * ? \" < > | or start with a dot";
        return false;
    }
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    for (auto& [key, label] : ui::builtin_themes())
        if (lower == key) {
            error = label + " is a built-in theme";
            return false;
        }
    std::error_code ec;
    if (std::filesystem::exists(ui::themes_dir() / (name + ".json"), ec)) {
        error = "There is a theme named '" + name + "'";
        return false;
    }
    return true;
}

void theme_editor::save()
{
    std::string e;
    if (!ui::save_theme(ui::current(), e)) error = e;
}

void theme_editor::name_popup(std::string& key, const std::function<void()>& load)
{
    using namespace ImGui;

    if (asking != naming::none && !IsPopupOpen("Theme name")) OpenPopup("Theme name");
    if (!BeginPopupModal("Theme name", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    TextUnformatted(asking == naming::duplicate ? "Name of the copy" : "New name");
    if (IsWindowAppearing()) SetKeyboardFocusHere();
    bool enter = InputText("##name", name_buf, sizeof name_buf, ImGuiInputTextFlags_EnterReturnsTrue);
    if (!error.empty()) TextColored(ui::color(ui::color_id::error), "%s", error.c_str());

    bool ok = Button("OK") || enter;
    SameLine();
    if (Button("Cancel") || IsKeyPressed(ImGuiKey_Escape)) {
        asking = naming::none;
        error.clear();
        CloseCurrentPopup();
    } else if (ok && check_name(name_buf)) {
        auto t = ui::current();
        auto old = t.file;
        t.name = name_buf;
        t.file = ui::themes_dir() / (t.name + ".json");
        std::string e;
        if (ui::save_theme(t, e)) {
            // renamed: the old file goes
            std::error_code ec;
            if (asking == naming::rename && !old.empty()) std::filesystem::remove(old, ec);
            key = ui::theme_key(t);
            load();
            error.clear();
            asking = naming::none;
            CloseCurrentPopup();
        } else {
            error = e;
        }
    }
    EndPopup();
}

void theme_editor::gui(std::string& key, const std::function<void()>& use_current, const std::function<void()>& load)
{
    using namespace ImGui;
    if (!open) return;

    SetNextWindowSize({GetFontSize() * 26, GetFontSize() * 32}, ImGuiCond_FirstUseEver);
    if (!Begin("Theme editor", &open, ImGuiWindowFlags_NoDocking)) {
        End();
        return;
    }

    if (ui::theme_combo("Theme", key)) load();
    auto& cur = ui::current();
    bool builtin = cur.builtin();

    if (Button("Duplicate...")) {
        asking = naming::duplicate;
        auto name = cur.name + " copy";
        std::snprintf(name_buf, sizeof name_buf, "%s", name.c_str());
        error.clear();
    }
    SetItemTooltip("A custom theme starting with these colors");
    if (!builtin) {
        SameLine();
        if (Button("Rename...")) {
            asking = naming::rename;
            std::snprintf(name_buf, sizeof name_buf, "%s", cur.name.c_str());
            error.clear();
        }
        SameLine();
        if (Button("Delete...")) OpenPopup("Delete theme");
    }
    name_popup(key, load);

    if (BeginPopupModal("Delete theme", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        Text("Delete the theme '%s'?", cur.name.c_str());
        if (Button("Delete")) {
            std::error_code ec;
            std::filesystem::remove(cur.file, ec);
            // its base is the closest
            key = cur.base;
            load();
            CloseCurrentPopup();
        }
        SameLine();
        if (Button("Cancel") || IsKeyPressed(ImGuiKey_Escape)) CloseCurrentPopup();
        EndPopup();
    }

    if (builtin) TextDisabled("Built-in themes can't be changed, duplicate this one to edit it");
    if (!error.empty() && asking == naming::none) TextColored(ui::color(ui::color_id::error), "%s", error.c_str());
    filter.Draw("Filter", GetFontSize() * 12);
    Separator();

    auto base = ui::builtin_theme(cur.base);
    auto& defs = ui::color_defs();

    // one row: the color, its name and a reset to the base theme. true when
    // it changed.
    auto row = [&](const char* name, const char* description, ImVec4& c, ImVec4 def) {
        bool changed = false;
        PushID(name);
        BeginDisabled(builtin);
        if (ColorEdit4("##color", &c.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) changed = true;
        bool done = IsItemDeactivatedAfterEdit();
        EndDisabled();
        SameLine();
        TextUnformatted(name);
        if (description && *description) SetItemTooltip("%s", description);
        if (!builtin && !same(c, def)) {
            SameLine();
            if (SmallButton("Reset")) {
                c = def;
                changed = done = true;
            }
            SetItemTooltip("Back to %s's color", base.name.c_str());
        }
        PopID();
        return std::pair{changed, done};
    };

    // the folder's line stays under the list
    BeginChild("colors", {0, -GetFrameHeightWithSpacing()});
    ui::theme edited = cur;
    bool changed = false, done = false;
    for (auto group : groups) {
        bool ui_group = std::strcmp(group, ui::interface_group) == 0;
        // groups with something passing the filter
        bool any = false;
        if (ui_group) {
            for (int i = 0; i < ImGuiCol_COUNT && !any; i++)
                any = filter.PassFilter(GetStyleColorName(i));
        } else {
            for (auto& d : defs)
                any = any || (std::strcmp(d.group, group) == 0 && (filter.PassFilter(d.name) || filter.PassFilter(d.description)));
        }
        if (!any) continue;
        // filtering shows what matches
        if (filter.IsActive()) SetNextItemOpen(true);
        if (!CollapsingHeader(group, ui_group ? 0 : ImGuiTreeNodeFlags_DefaultOpen)) continue;

        if (ui_group) {
            for (int i = 0; i < ImGuiCol_COUNT; i++) {
                if (!filter.PassFilter(GetStyleColorName(i))) continue;
                auto [c, d] = row(GetStyleColorName(i), nullptr, edited.ui[i], base.ui[i]);
                changed |= c;
                done |= d;
            }
        } else {
            for (int i = 0; i < ui::color_count; i++) {
                auto& d = defs[i];
                if (std::strcmp(d.group, group) != 0 || !(filter.PassFilter(d.name) || filter.PassFilter(d.description))) continue;
                auto [c, dn] = row(d.name, d.description, edited.colors[i], base.colors[i]);
                changed |= c;
                done |= dn;
            }
        }
    }
    EndChild();
    auto dir = ui::themes_dir().string();
    TextDisabled("Custom themes: %s", dir.c_str());

    if (changed && !builtin) {
        ui::set_current(std::move(edited));
        use_current();
    }
    // saved once a drag or text edit ends, not every frame
    if (done && !builtin) save();

    End();
}

}
