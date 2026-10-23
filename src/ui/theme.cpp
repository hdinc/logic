#include "theme.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace logic::ui {

namespace {

constexpr ImVec4 rgb(unsigned hex, float a = 1.0f)
{
    return {((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f, a};
}

const std::array<color_def, color_count> defs{{
    {"background", "Background", "Canvas", "behind schematics and symbols", rgb(0x15171a), rgb(0xfafaf8), rgb(0x000000)},
    {"grid", "Grid", "Canvas", "grid dots, lines and checkers", rgb(0x6a7080, 0.8f), rgb(0x9aa0ab, 0.8f), rgb(0x3a3f47, 0.8f)},
    {"symbol", "Symbol", "Canvas", "outlines and text of symbols, port connectors", rgb(0xe4e7eb), rgb(0x1e2126), rgb(0xf2f4f7)},
    {"symbol_fill", "Symbol fill", "Canvas", "inside closed outlines of symbols", rgb(0x1f2b38), rgb(0xdce8f5), rgb(0x0d1420)},
    {"wire", "Wire", "Canvas", "single bit wires while editing", rgb(0xa2a9b3), rgb(0x4d545e), rgb(0xb4bac4)},
    {"bus_wire", "Bus wire", "Canvas", "wires of buses while editing", rgb(0x6fa8dc), rgb(0x2f6fb5), rgb(0x5fa8ff)},
    {"bundle_wire", "Bundle wire", "Canvas", "wires of bundles while editing", rgb(0xb98ae6), rgb(0x8a3fc4), rgb(0xc08cff)},
    {"label", "Instance name", "Canvas", "names of instances", rgb(0x8a919c), rgb(0x737a85), rgb(0x9aa1ab)},
    {"selection", "Selection", "Canvas", "selected elements, the highlighted net", rgb(0xffc533), rgb(0xf08c00), rgb(0xffcc33)},
    {"guide", "Guide", "Canvas", "guide lines while drawing", rgb(0x6f8a33), rgb(0x8ca640), rgb(0x7a9a35)},
    {"pin", "Pin", "Canvas", "pins in the symbol editor", rgb(0x5a9bff), rgb(0x1a66e6), rgb(0x4d9bff)},
    {"control_point", "Control point", "Canvas", "points of shapes in the symbol editor", rgb(0xff4d4d), rgb(0xd91a1a), rgb(0xff4d4d)},
    {"preview", "Preview", "Canvas", "what is being placed or drawn, the origin", rgb(0x8a919c), rgb(0x9097a3), rgb(0x8a919c)},
    {"width_error", "Width error", "Canvas", "wires connecting ports of different widths or types", rgb(0xff8c1a), rgb(0xf07800), rgb(0xff9a2e)},

    {"high", "High", "Signals", "1 bits while simulating", rgb(0x3ee65a), rgb(0x12b333), rgb(0x45ff66)},
    {"low", "Low", "Signals", "0 bits while simulating", rgb(0x1d7a2f), rgb(0x0d6120), rgb(0x1f8a35)},
    {"unknown", "Unknown", "Signals", "unknown values (x) on wires, in values and waveforms", rgb(0xff4d4d), rgb(0xe01b1b), rgb(0xff4d4d)},
    {"floating", "Floating", "Signals", "values not driven (z) on wires, in values and waveforms", rgb(0x5a8cff), rgb(0x2966f0), rgb(0x5a8cff)},
    {"bus", "Bus value", "Signals", "known values of buses while simulating", rgb(0x8cc8f2), rgb(0x1f7acc), rgb(0x8cd0ff)},
    {"bundle", "Bundle value", "Signals", "known values of bundles while simulating", rgb(0xd2b0f5), rgb(0x9b4fd9), rgb(0xdcb8ff)},
    {"wave_value", "Waveform value", "Signals", "known values in waveforms", rgb(0x50c882), rgb(0x50c882), rgb(0x50dc8c)},

    {"cursor_a", "Cursor A", "Waveforms", "the main cursor of waveforms", rgb(0xf0c846), rgb(0xf0c846), rgb(0xffd24a)},
    {"cursor_b", "Cursor B", "Waveforms", "the second cursor of waveforms", rgb(0x5ac8f0), rgb(0x5ac8f0), rgb(0x5ad2ff)},
    {"marker", "Marker", "Waveforms", "markers in waveforms", rgb(0xc882f0), rgb(0xc882f0), rgb(0xd28cff)},

    {"error", "Error", "Messages", "errors, missing cells on the canvas", rgb(0xff4d4d), rgb(0xd91a1a), rgb(0xff5555)},
    {"warning", "Warning", "Messages", "problems, locked cells, a paused or slow simulation", ImVec4(1, 0.6f, 0.2f, 1), ImVec4(1, 0.6f, 0.2f, 1), rgb(0xff9933)},
    {"info", "Info", "Messages", "work going on, e.g. building HDL models", ImVec4(0.4f, 0.7f, 1, 1), ImVec4(0.4f, 0.7f, 1, 1), rgb(0x66b3ff)},
    {"success", "Success", "Messages", "work done, e.g. a built HDL model", ImVec4(0.4f, 0.9f, 0.4f, 1), ImVec4(0.4f, 0.9f, 0.4f, 1), rgb(0x66e666)},
    {"program_counter", "Program counter", "Messages", "the instruction at the program counter in the disassembly", ImVec4(1, 0.85f, 0.3f, 1),
        ImVec4(1, 0.85f, 0.3f, 1), rgb(0xffd94d)},

    {"display_red", "Red", "Displays", "LEDs and 7-segment displays in red", ImVec4(1, 0.15f, 0.1f, 1), ImVec4(1, 0.15f, 0.1f, 1), ImVec4(1, 0.15f, 0.1f, 1)},
    {"display_green", "Green", "Displays", "LEDs and 7-segment displays in green", ImVec4(0.2f, 0.9f, 0.3f, 1), ImVec4(0.2f, 0.9f, 0.3f, 1), ImVec4(0.2f, 0.9f, 0.3f, 1)},
    {"display_yellow", "Yellow", "Displays", "LEDs and 7-segment displays in yellow", ImVec4(1, 0.85f, 0.1f, 1), ImVec4(1, 0.85f, 0.1f, 1), ImVec4(1, 0.85f, 0.1f, 1)},
    {"display_orange", "Orange", "Displays", "LEDs and 7-segment displays in orange", ImVec4(1, 0.5f, 0.05f, 1), ImVec4(1, 0.5f, 0.05f, 1), ImVec4(1, 0.5f, 0.05f, 1)},
    {"display_blue", "Blue", "Displays", "LEDs and 7-segment displays in blue", ImVec4(0.3f, 0.55f, 1, 1), ImVec4(0.3f, 0.55f, 1, 1), ImVec4(0.3f, 0.55f, 1, 1)},
    {"display_cyan", "Cyan", "Displays", "LEDs and 7-segment displays in cyan", ImVec4(0.2f, 0.9f, 0.95f, 1), ImVec4(0.2f, 0.9f, 0.95f, 1), ImVec4(0.2f, 0.9f, 0.95f, 1)},
    {"display_white", "White", "Displays", "LEDs and 7-segment displays in white", ImVec4(0.95f, 0.95f, 0.95f, 1), ImVec4(0.95f, 0.95f, 0.95f, 1), ImVec4(0.95f, 0.95f, 0.95f, 1)},
    {"display_unlit", "Unlit segment", "Displays", "segments of 7-segment displays that are off", rgb(0x46515c), rgb(0xb6c0cc), rgb(0x1e242b)},
}};

theme current_theme = builtin_theme("dark");

// imgui's dark colors on black: windows, frames and title bars darker
void make_black(ImVec4* c)
{
    c[ImGuiCol_WindowBg] = rgb(0x000000);
    c[ImGuiCol_ChildBg] = rgb(0x000000, 0.0f);
    c[ImGuiCol_PopupBg] = rgb(0x0a0a0a, 0.97f);
    c[ImGuiCol_Border] = rgb(0x2e2e33);
    c[ImGuiCol_FrameBg] = rgb(0x141414);
    c[ImGuiCol_FrameBgHovered] = rgb(0x1f2a3a);
    c[ImGuiCol_FrameBgActive] = rgb(0x26364d);
    c[ImGuiCol_TitleBg] = rgb(0x000000);
    c[ImGuiCol_TitleBgActive] = rgb(0x0d1a2b);
    c[ImGuiCol_TitleBgCollapsed] = rgb(0x000000);
    c[ImGuiCol_MenuBarBg] = rgb(0x0a0a0a);
    c[ImGuiCol_ScrollbarBg] = rgb(0x000000);
    c[ImGuiCol_TableHeaderBg] = rgb(0x141414);
    c[ImGuiCol_TableBorderStrong] = rgb(0x2e2e33);
    c[ImGuiCol_TableBorderLight] = rgb(0x1e1e22);
    c[ImGuiCol_DockingEmptyBg] = rgb(0x000000);
    c[ImGuiCol_Separator] = rgb(0x2e2e33);
}

}

const std::array<color_def, color_count>& color_defs()
{
    return defs;
}

const std::vector<std::pair<std::string, std::string>>& builtin_themes()
{
    static const std::vector<std::pair<std::string, std::string>> r{{"dark", "Dark"}, {"light", "Light"}, {"black", "Black"}};
    return r;
}

theme builtin_theme(const std::string& key)
{
    bool light = key == "light", black = key == "black";
    theme t;
    t.name = light ? "Light" : black ? "Black" : "Dark";
    t.base = light ? "light" : black ? "black" : "dark";
    for (int i = 0; i < color_count; i++)
        t.colors[i] = light ? defs[i].light : black ? defs[i].black : defs[i].dark;
    // without a context: imgui only writes into the given style
    ImGuiStyle s;
    if (light)
        ImGui::StyleColorsLight(&s);
    else
        ImGui::StyleColorsDark(&s);
    if (black) make_black(s.Colors);
    std::copy(std::begin(s.Colors), std::end(s.Colors), t.ui.begin());
    return t;
}

const theme& current()
{
    return current_theme;
}

void set_current(theme t)
{
    current_theme = std::move(t);
}

ImVec4 color(color_id c)
{
    return current_theme[c];
}

ImU32 color_u32(color_id c)
{
    return ImGui::ColorConvertFloat4ToU32(current_theme[c]);
}

void apply_current()
{
    auto& s = ImGui::GetStyle();
    std::copy(current_theme.ui.begin(), current_theme.ui.end(), std::begin(s.Colors));
    s.FrameRounding = 4.0f;
    s.GrabRounding = 4.0f;
    s.WindowRounding = 6.0f;
    s.PopupRounding = 4.0f;
    s.TabRounding = 4.0f;
    // outlined inputs and buttons, lines connecting tree nodes
    s.FrameBorderSize = 1.0f;
    s.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesToNodes;
}

std::filesystem::path config_dir()
{
    auto env = [](const char* name) -> std::filesystem::path {
        auto v = std::getenv(name);
        return v && *v ? std::filesystem::path(v) : std::filesystem::path();
    };
#if defined(_WIN32)
    if (auto d = env("APPDATA"); !d.empty()) return d / "logic";
#elif defined(__APPLE__)
    if (auto d = env("HOME"); !d.empty()) return d / "Library" / "Application Support" / "logic";
#else
    if (auto d = env("XDG_CONFIG_HOME"); !d.empty()) return d / "logic";
    if (auto d = env("HOME"); !d.empty()) return d / ".config" / "logic";
#endif
    return "logic-config";
}

std::filesystem::path themes_dir()
{
    return config_dir() / "themes";
}

std::vector<std::filesystem::path> list_themes()
{
    std::vector<std::filesystem::path> r;
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(themes_dir(), ec))
        if (e.is_regular_file(ec) && e.path().extension() == ".json") r.push_back(e.path());
    std::sort(r.begin(), r.end());
    return r;
}

std::string to_hex(ImVec4 c)
{
    auto b = [](float v) { return (int)(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    char s[16];
    std::snprintf(s, sizeof s, "#%02x%02x%02x%02x", b(c.x), b(c.y), b(c.z), b(c.w));
    return s;
}

bool parse_hex(const std::string& s, ImVec4& out)
{
    if (s.size() != 7 && s.size() != 9) return false;
    if (s[0] != '#') return false;
    unsigned v[4] = {0, 0, 0, 255};
    for (size_t i = 0; i < (s.size() - 1) / 2; i++) {
        auto part = s.substr(1 + 2 * i, 2);
        if (!std::isxdigit((unsigned char)part[0]) || !std::isxdigit((unsigned char)part[1])) return false;
        v[i] = (unsigned)std::stoul(part, nullptr, 16);
    }
    out = {v[0] / 255.0f, v[1] / 255.0f, v[2] / 255.0f, v[3] / 255.0f};
    return true;
}

bool load_theme(const std::filesystem::path& file, theme& out, std::string& error)
{
    std::ifstream in(file);
    if (!in) {
        error = "can't open " + file.string();
        return false;
    }
    auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        error = file.string() + " isn't a theme file";
        return false;
    }

    auto base = j.value("base", std::string("dark"));
    out = builtin_theme(base);
    out.name = j.value("name", file.stem().string());
    out.file = file;
    auto read = [](const nlohmann::json& obj, const char* key, ImVec4& c) {
        auto it = obj.find(key);
        if (it == obj.end() || !it->is_string()) return;
        ImVec4 v;
        if (parse_hex(it->get<std::string>(), v)) c = v;
    };
    if (auto it = j.find("colors"); it != j.end() && it->is_object())
        for (int i = 0; i < color_count; i++)
            read(*it, defs[i].key, out.colors[i]);
    if (auto it = j.find("interface"); it != j.end() && it->is_object())
        for (int i = 0; i < ImGuiCol_COUNT; i++)
            read(*it, ImGui::GetStyleColorName(i), out.ui[i]);
    return true;
}

bool save_theme(const theme& t, std::string& error)
{
    nlohmann::ordered_json j;
    j["name"] = t.name;
    j["base"] = t.base;
    auto& colors = j["colors"];
    for (int i = 0; i < color_count; i++)
        colors[defs[i].key] = to_hex(t.colors[i]);
    auto& ui = j["interface"];
    for (int i = 0; i < ImGuiCol_COUNT; i++)
        ui[ImGui::GetStyleColorName(i)] = to_hex(t.ui[i]);

    std::error_code ec;
    std::filesystem::create_directories(t.file.parent_path(), ec);
    std::ofstream out(t.file);
    if (out) out << j.dump(2) << '\n';
    if (!out) {
        error = "can't write " + t.file.string();
        return false;
    }
    return true;
}

bool find_theme(const std::string& name, theme& out, std::string& error)
{
    for (auto& [key, label] : builtin_themes())
        if (name == key) {
            out = builtin_theme(key);
            return true;
        }
    if (name.empty()) {
        out = builtin_theme("dark");
        return true;
    }
    if (load_theme(themes_dir() / (name + ".json"), out, error)) return true;
    out = builtin_theme("dark");
    return false;
}

std::string theme_key(const theme& t)
{
    return t.builtin() ? t.base : t.file.stem().string();
}

namespace {

// keys and names of the themes to choose from
std::vector<std::pair<std::string, std::string>> theme_choices()
{
    auto r = builtin_themes();
    for (auto& f : list_themes())
        r.push_back({f.stem().string(), f.stem().string()});
    return r;
}

}

bool theme_combo(const char* label, std::string& key)
{
    auto choices = theme_choices();
    auto it = std::find_if(choices.begin(), choices.end(), [&](auto& c) { return c.first == key; });
    bool changed = false;
    if (ImGui::BeginCombo(label, it != choices.end() ? it->second.c_str() : key.c_str())) {
        for (size_t i = 0; i < choices.size(); i++) {
            // the custom themes after the built-in ones
            if (i == builtin_themes().size()) ImGui::Separator();
            if (ImGui::Selectable(choices[i].second.c_str(), choices[i].first == key) && choices[i].first != key) {
                key = choices[i].first;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool theme_menu_items(std::string& key)
{
    bool changed = false;
    auto choices = theme_choices();
    for (size_t i = 0; i < choices.size(); i++) {
        if (i == builtin_themes().size()) ImGui::Separator();
        if (ImGui::MenuItem(choices[i].second.c_str(), nullptr, choices[i].first == key) && choices[i].first != key) {
            key = choices[i].first;
            changed = true;
        }
    }
    return changed;
}

}
