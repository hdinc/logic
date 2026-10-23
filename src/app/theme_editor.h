#pragma once

#include "../ui/theme.h"

#include <functional>
#include <string>

#include <imgui.h>

namespace logic::app {

// edits the colors of the current theme (ui::current()). the built-in themes
// are read only, a duplicate is a custom theme saved in ui::themes_dir().
// custom themes are named like their files.
class theme_editor {
public:
    bool open = false;

    // key: the theme chosen in the settings, changed when a theme is
    // duplicated, renamed or deleted. use_current: shows ui::current() after
    // it changed. load: loads and shows the theme of key.
    void gui(std::string& key, const std::function<void()>& use_current, const std::function<void()>& load);

private:
    // asks for a name, then duplicates or renames
    enum class naming { none, duplicate, rename };
    void name_popup(std::string& key, const std::function<void()>& load);
    void save();
    // false if a theme by that name can't be made
    bool check_name(const std::string& name);

    naming asking = naming::none;
    char name_buf[64] = {};
    std::string error;
    ImGuiTextFilter filter;
};

}
