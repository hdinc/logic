// tests for themes: the color table, the built-in themes and theme files.
// no imgui context is needed.

#include "../src/ui/theme.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>

using namespace logic;

static int failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static bool same(ImVec4 a, ImVec4 b)
{
    return ui::to_hex(a) == ui::to_hex(b);
}

static void test_table()
{
    std::set<std::string> keys, names;
    for (auto& d : ui::color_defs()) {
        CHECK(d.key && *d.key && d.name && *d.name && d.group && *d.group && d.description && *d.description);
        keys.insert(d.key);
        // names are unique within a group
        names.insert(std::string(d.group) + "/" + d.name);
    }
    CHECK((int)keys.size() == ui::color_count);
    CHECK((int)names.size() == ui::color_count);
    for (const char* g : {"Canvas", "Signals", "Waveforms", "Messages", "Displays"}) {
        int n = 0;
        for (auto& d : ui::color_defs())
            n += std::strcmp(d.group, g) == 0;
        CHECK(n > 0);
    }
}

static void test_hex()
{
    ImVec4 c;
    CHECK(ui::parse_hex("#15171a", c) && ui::to_hex(c) == "#15171aff");
    CHECK(ui::parse_hex("#ff8c1a80", c) && ui::to_hex(c) == "#ff8c1a80");
    CHECK(!ui::parse_hex("15171a", c));
    CHECK(!ui::parse_hex("#15171", c));
    CHECK(!ui::parse_hex("#zz171a", c));
}

static void test_builtin()
{
    auto dark = ui::builtin_theme("dark");
    auto light = ui::builtin_theme("light");
    auto black = ui::builtin_theme("black");
    CHECK(dark.builtin() && dark.base == "dark" && light.base == "light" && dark.name == "Dark" && light.name == "Light");
    CHECK(black.builtin() && black.base == "black" && black.name == "Black");
    CHECK(ui::builtin_themes().size() == 3);
    // unknown keys give Dark
    CHECK(ui::builtin_theme("nope").base == "dark");
    CHECK(ui::to_hex(dark[ui::color_id::background]) == "#15171aff");
    CHECK(ui::to_hex(light[ui::color_id::pin]) == "#1a66e6ff");
    // imgui's own colors
    ImGuiStyle s;
    ImGui::StyleColorsLight(&s);
    CHECK(same(light.ui[ImGuiCol_WindowBg], s.Colors[ImGuiCol_WindowBg]));
    CHECK(!same(dark.ui[ImGuiCol_WindowBg], light.ui[ImGuiCol_WindowBg]));
    CHECK(ui::theme_key(dark) == "dark" && ui::theme_key(light) == "light" && ui::theme_key(black) == "black");

    // black: a black canvas and windows, imgui's dark accents
    CHECK(ui::to_hex(black[ui::color_id::background]) == "#000000ff");
    CHECK(ui::to_hex(black.ui[ImGuiCol_WindowBg]) == "#000000ff");
    CHECK(same(black.ui[ImGuiCol_Button], dark.ui[ImGuiCol_Button]));
    std::string error;
    ui::theme found;
    CHECK(ui::find_theme("black", found, error) && found.name == "Black");
}

static void test_files(const std::filesystem::path& dir)
{
    std::filesystem::remove_all(dir);

    // a custom theme survives saving and loading
    auto t = ui::builtin_theme("light");
    t.name = "Mine";
    t.file = dir / "Mine.json";
    t.colors[(int)ui::color_id::error] = {0.1f, 0.2f, 0.3f, 0.4f};
    t.ui[ImGuiCol_Button] = {1, 0, 1, 1};
    std::string error;
    CHECK(ui::save_theme(t, error));
    ui::theme back;
    CHECK(ui::load_theme(t.file, back, error));
    CHECK(back.name == "Mine" && back.base == "light" && back.file == t.file && !back.builtin());
    CHECK(ui::theme_key(back) == "Mine");
    for (int i = 0; i < ui::color_count; i++)
        CHECK(same(back.colors[i], t.colors[i]));
    for (int i = 0; i < ImGuiCol_COUNT; i++)
        CHECK(same(back.ui[i], t.ui[i]));

    // missing and invalid colors come from the base, unknown keys are ignored
    {
        std::ofstream out(dir / "partial.json");
        out << R"({"base": "dark", "colors": {"wire": "#010203", "high": "green", "nonsense": "#ffffff"},
                  "interface": {"Text": "#ff0000ff", "Nope": "#000000"}})";
    }
    ui::theme p;
    CHECK(ui::load_theme(dir / "partial.json", p, error));
    auto dark = ui::builtin_theme("dark");
    CHECK(p.name == "partial" && p.base == "dark");

    // a custom theme based on black takes its gaps from Black
    {
        std::ofstream out(dir / "on_black.json");
        out << R"({"base": "black", "colors": {"wire": "#010203"}})";
    }
    ui::theme b;
    CHECK(ui::load_theme(dir / "on_black.json", b, error));
    CHECK(b.base == "black" && ui::to_hex(b[ui::color_id::background]) == "#000000ff" && ui::to_hex(b.ui[ImGuiCol_WindowBg]) == "#000000ff");
    CHECK(ui::to_hex(p[ui::color_id::wire]) == "#010203ff");
    CHECK(same(p[ui::color_id::high], dark[ui::color_id::high]));
    CHECK(same(p[ui::color_id::background], dark[ui::color_id::background]));
    CHECK(ui::to_hex(p.ui[ImGuiCol_Text]) == "#ff0000ff");
    CHECK(same(p.ui[ImGuiCol_WindowBg], dark.ui[ImGuiCol_WindowBg]));

    // not a theme
    {
        std::ofstream out(dir / "broken.json");
        out << "{ not json";
    }
    CHECK(!ui::load_theme(dir / "broken.json", p, error) && !error.empty());
    CHECK(!ui::load_theme(dir / "missing.json", p, error));

    std::filesystem::remove_all(dir);
}

int main(int argc, char** argv)
{
    auto dir = std::filesystem::path(argc > 1 ? argv[1] : ".") / "theme_test_dir";

    test_table();
    test_hex();
    test_builtin();
    test_files(dir);

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
