// themes: every color the apps use, named, with dark and light defaults.
// custom themes are json files in the user's config folder.

#pragma once

#include <imgui.h>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace logic::ui {

// the colors of the apps besides imgui's own. uses that mean the same share
// one color, e.g. unknown values on the canvas and in waveforms.
enum class color_id {
    // canvas
    background,
    grid,
    symbol,
    symbol_fill,
    wire,
    bus_wire,
    bundle_wire,
    label,
    selection,
    guide,
    pin,
    control_point,
    preview,
    width_error,
    // signal values
    high,
    low,
    unknown,
    floating,
    bus,
    bundle,
    wave_value,
    // waveforms
    cursor_a,
    cursor_b,
    marker,
    // messages
    error,
    warning,
    info,
    success,
    program_counter,
    // led and 7-segment display colors
    display_red,
    display_green,
    display_yellow,
    display_orange,
    display_blue,
    display_cyan,
    display_white,
    display_unlit,
    count
};

constexpr int color_count = (int)color_id::count;

struct color_def {
    // in theme files
    const char* key;
    const char* name;
    const char* group;
    // where it is used
    const char* description;
    ImVec4 dark;
    ImVec4 light;
    ImVec4 black;
};

// in the order of color_id
const std::array<color_def, color_count>& color_defs();

// the group of imgui's colors in the editor and the key in theme files
constexpr const char* interface_group = "Interface";

struct theme {
    std::string name;
    // the built-in theme missing colors come from: "dark", "light" or
    // "black"
    std::string base = "dark";
    std::array<ImVec4, color_count> colors{};
    // imgui's colors, by ImGuiCol
    std::array<ImVec4, ImGuiCol_COUNT> ui{};
    // the file of a custom theme, empty for the built-in ones
    std::filesystem::path file;

    bool builtin() const { return file.empty(); }
    ImVec4 operator[](color_id c) const { return colors[(int)c]; }
};

// the built-in themes by key and name: Dark, Light and Black
const std::vector<std::pair<std::string, std::string>>& builtin_themes();
// a built-in theme by its key, Dark if there is none: the defaults of the
// color table and imgui's colors (black: imgui's dark ones on black)
theme builtin_theme(const std::string& key);

// the theme in use
const theme& current();
void set_current(theme t);
ImVec4 color(color_id c);
ImU32 color_u32(color_id c);

// imgui's colors of the current theme, the roundings and borders of the apps
void apply_current();

// the user's settings folder of the apps, e.g. ~/.config/logic
std::filesystem::path config_dir();
// where custom themes are kept
std::filesystem::path themes_dir();

// the custom themes' files in themes_dir(), sorted
std::vector<std::filesystem::path> list_themes();
// a theme file. colors that are missing or invalid come from its base,
// unknown keys are ignored. false with error set if it can't be read.
bool load_theme(const std::filesystem::path& file, theme& out, std::string& error);
// writes t to t.file, creating the folder
bool save_theme(const theme& t, std::string& error);
// a theme by its name in settings: a built-in key or the file name without
// .json of a custom theme in themes_dir(). false with error set if it can't
// be loaded, out is then Dark.
bool find_theme(const std::string& name, theme& out, std::string& error);
// the name find_theme takes
std::string theme_key(const theme& t);

// choosing a theme by its key (see find_theme): Dark, Light and the custom
// themes. true when the user picked another one.
bool theme_combo(const char* label, std::string& key);
bool theme_menu_items(std::string& key);

// "#rrggbbaa", and back ("#rrggbb" or "#rrggbbaa"). false if invalid.
std::string to_hex(ImVec4 c);
bool parse_hex(const std::string& s, ImVec4& out);

}
