#include "app.h"
#include "../builtin/builtin.h"
#include "../editor/param_editor.h"
#include "../io/storage.h"
#include "../plugin/libraries.h"
#include "../ui/style.h"

#include <Hd/Resource.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace logic::app {

app::app(Hd::Window& w, std::filesystem::path r)
    : window(w)
    , gui(&w)
    , root(std::move(r))
    , factory({&builtin::factory(), &plugins, &hdl})
    , components(renderer)
    , editor_tex(Hd::Texture::emptyTexture(3840, 2160))
    , editor_fb(editor_tex)
{
    for (auto& e : io::load_workspace(ws, root))
        message(e);

    if (!ws.find_library("work")) ws.add_library("work").dir = root / "work";

    ws.add_builtin_library(builtin::registry());
    plugins.add_default_dirs(root);
    ws.add_provider(plugins);
    ws.add_provider(hdl);
    reload_plugins();
}

app::~app()
{
    // imgui saves imgui.ini when gui is destroyed, after the members the
    // settings handler reads
    auto& io = ImGui::GetIO();
    if (io.IniFilename) ImGui::SaveIniSettingsToDisk(io.IniFilename);
    io.IniFilename = nullptr;
}

void app::reload_plugins()
{
    for (auto& m : plugin::sync_libraries(ws, plugins, root / "plugins"))
        message(m);
    // stable order in the library panel: the user's, builtin, plugins
    auto rank = [](auto& l) { return !l->read_only ? 0 : l->symbols_dir.empty() ? 1 : 2; };
    std::stable_sort(ws.libraries.begin(), ws.libraries.end(), [&](auto& a, auto& b) { return rank(a) < rank(b); });
}

void app::run()
{
    apply_theme();
    ui::set_font_size((float)font_size);
    // read with the rest of imgui.ini on the first frame
    add_settings_handler();

    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    gui.Functions.add([&] { draw_gui(); });

    while (!should_exit) {
        window.PollEvents();

        if (window.ShouldClose()) {
            glfwSetWindowShouldClose(window.Id(), false);
            if (has_unsaved())
                exit_requested = true;
            else
                should_exit = true;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClear(GL_COLOR_BUFFER_BIT);

        if (font_update_pending) update_font();
        if (canvas_font_pending) update_canvas_font();
        gui.Draw();

        window.SwapBuffers();
    }
}

void app::update_font()
{
    // between frames, nothing is drawing with the old fonts
    auto f = Hd::Resource::getExeRes("res/fonts/Roboto.ttf");
    std::vector<std::string> errors;
    ui::load_fonts(ui_font, mono_font, {(const char*)f.first, (size_t)f.second}, errors);
    for (auto& e : errors)
        message(e);
    font_update_pending = false;
}

void app::update_canvas_font()
{
    canvas_font_pending = false;
    auto builtin = (const unsigned char*)Hd::Resource::getExeRes("res/fonts/Roboto.ttf").first;
    if (canvas_font.builtin()) {
        renderer.g.setFont(builtin);
        return;
    }
    std::string error;
    auto data = ui::read_font(canvas_font, error);
    try {
        if (data.empty()) throw std::runtime_error(error);
        // the font keeps a copy
        renderer.g.setFont(data.data(), canvas_font.index);
    } catch (std::exception& e) {
        message("canvas font " + canvas_font.file.string() + ": " + e.what() + ", using the default font");
        renderer.g.setFont(builtin);
    }
}

namespace {

// for imgui.ini
const char* grid_style_name(render::grid_style g)
{
    switch (g) {
    case render::grid_style::lines: return "lines";
    case render::grid_style::checkers: return "checkers";
    default: return "dots";
    }
}

render::grid_style grid_style_from(const char* s)
{
    if (std::strcmp(s, "lines") == 0) return render::grid_style::lines;
    if (std::strcmp(s, "checkers") == 0) return render::grid_style::checkers;
    return render::grid_style::dots;
}

}

void app::add_settings_handler()
{
    ImGuiSettingsHandler h;
    h.TypeName = "Logic";
    h.TypeHash = ImHashStr("Logic");
    h.UserData = this;
    h.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char* name) -> void* {
        return std::strcmp(name, "Settings") == 0 ? (void*)1 : nullptr;
    };
    h.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler* h, void*, const char* line) {
        auto a = (app*)h->UserData;
        char str[32];
        int i;
        if (std::sscanf(line, "Theme=%31s", str) == 1)
            a->light_theme = std::strcmp(str, "light") == 0;
        else if (std::sscanf(line, "Grid=%d", &i) == 1)
            a->renderer.show_grid = i != 0;
        else if (std::sscanf(line, "GridStyle=%31s", str) == 1)
            a->renderer.grid = grid_style_from(str);
        else if (std::sscanf(line, "FillSymbols=%d", &i) == 1)
            a->renderer.fill_symbols = i != 0;
        else if (std::sscanf(line, "ThickBuses=%d", &i) == 1)
            a->renderer.thick_buses = i != 0;
        else if (std::sscanf(line, "InstanceNames=%31s", str) == 1)
            a->renderer.names = std::strcmp(str, "none") == 0 ? render::instance_names::none
                : std::strcmp(str, "custom") == 0             ? render::instance_names::custom
                                                              : render::instance_names::all;
        else if (std::sscanf(line, "ComponentBar=%31s", str) == 1)
            a->components.dir = std::strcmp(str, "horizontal") == 0 ? component_bar::orientation::horizontal : component_bar::orientation::vertical;
        else if (std::sscanf(line, "FontSize=%d", &i) == 1) {
            int size = std::clamp(i, 12, 40);
            a->font_size = size;
            ui::set_font_size((float)size);
        } else if (std::strncmp(line, "UiFont=", 7) == 0) {
            a->ui_font = ui::parse_font_ref(line + 7);
            a->font_update_pending = true;
        } else if (std::strncmp(line, "MonoFont=", 9) == 0) {
            a->mono_font = ui::parse_font_ref(line + 9);
            a->font_update_pending = true;
        } else if (std::strncmp(line, "CanvasFont=", 11) == 0) {
            a->canvas_font = ui::parse_font_ref(line + 11);
            // the renderer starts with the built-in one
            a->canvas_font_pending = !a->canvas_font.builtin();
        }
    };
    h.ApplyAllFn = [](ImGuiContext*, ImGuiSettingsHandler* h) {
        auto a = (app*)h->UserData;
        a->apply_theme();
        a->saved_settings = a->current_settings();
    };
    h.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* h, ImGuiTextBuffer* buf) {
        auto a = (app*)h->UserData;
        buf->appendf("[Logic][Settings]\nTheme=%s\nGrid=%d\nGridStyle=%s\nComponentBar=%s\nFontSize=%d\n", a->light_theme ? "light" : "dark", a->renderer.show_grid ? 1 : 0, grid_style_name(a->renderer.grid), a->components.dir == component_bar::orientation::horizontal ? "horizontal" : "vertical", a->font_size);
        // the file's path is the rest of the line
        const char* names[] = {"all", "custom", "none"};
        buf->appendf("InstanceNames=%s\nThickBuses=%d\nFillSymbols=%d\n", names[(int)a->renderer.names], a->renderer.thick_buses ? 1 : 0, a->renderer.fill_symbols ? 1 : 0);
        buf->appendf("UiFont=%s\nMonoFont=%s\nCanvasFont=%s\n\n", ui::to_string(a->ui_font).c_str(), ui::to_string(a->mono_font).c_str(), ui::to_string(a->canvas_font).c_str());
    };
    ImGui::AddSettingsHandler(&h);
    saved_settings = current_settings();
}

app::settings app::current_settings() const
{
    return {light_theme, renderer.show_grid, renderer.grid, renderer.names, renderer.thick_buses, renderer.fill_symbols, components.dir, font_size, ui_font, mono_font, canvas_font};
}

void app::apply_theme()
{
    render::set_theme(light_theme ? render::light_theme() : render::dark_theme());
    ui::apply_style(light_theme);
}

void app::settings_window()
{
    using namespace ImGui;

    // also changed from the view menu and with G in the editors
    auto current = current_settings();
    if (current != saved_settings) {
        saved_settings = current;
        MarkIniSettingsDirty();
    }

    if (!show_settings) return;

    // fits the contents when opened, imgui.ini may have an older height
    SetNextWindowSize({GetFontSize() * 27, 0}, ImGuiCond_Appearing);
    SetNextWindowPos(GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    if (Begin("Settings", &show_settings, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        SeparatorText("Appearance");
        int theme = light_theme ? 1 : 0;
        if (Combo("Theme", &theme, "Dark\0Light\0")) {
            light_theme = theme == 1;
            apply_theme();
        }
        int size = font_size;
        SliderInt("Font size", &size, 12, 40, "%d px", ImGuiSliderFlags_AlwaysClamp);
        SameLine();
        if (Button("Reset")) size = 20;
        if (size != font_size) {
            font_size = size;
            ui::set_font_size((float)size);
        }

        SeparatorText("Fonts");
        if (!system_fonts) system_fonts = ui::system_fonts();
        if (ui_font_picker.gui("Interface", ui_font, *system_fonts, "Roboto (built-in)")) font_update_pending = true;
        std::string system_mono = "System monospace";
        auto default_mono = ui::default_mono_font();
        for (auto& f : *system_fonts) {
            if (default_mono.builtin() || f.ref != default_mono) continue;
            system_mono += " (" + f.family + ")";
            break;
        }
        if (mono_font_picker.gui("Monospace", mono_font, *system_fonts, system_mono.c_str())) font_update_pending = true;
        SetItemTooltip("Disassembly, memory, terminal and messages");
        if (canvas_font_picker.gui("Canvas", canvas_font, *system_fonts, "Roboto (built-in)")) canvas_font_pending = true;
        SetItemTooltip("Texts in schematics and symbols");
        if (Button("Rescan fonts")) system_fonts = ui::system_fonts();
        SetItemTooltip("Finds fonts installed since the program started");

        SeparatorText("Canvas");
        Checkbox("Show grid", &renderer.show_grid);
        SetItemTooltip("G in an editor");

        BeginDisabled(!renderer.show_grid);
        int style = (int)renderer.grid;
        TextUnformatted("Grid style");
        SameLine();
        RadioButton("Dots", &style, (int)render::grid_style::dots);
        SameLine();
        RadioButton("Lines", &style, (int)render::grid_style::lines);
        SameLine();
        RadioButton("Checkers", &style, (int)render::grid_style::checkers);
        renderer.grid = (render::grid_style)style;
        EndDisabled();

        Checkbox("Fill symbols", &renderer.fill_symbols);
        SetItemTooltip("Closed outlines of symbols in the theme's color, cells can have their own (symbol properties)");
        Checkbox("Thick buses and bundles", &renderer.thick_buses);
        SetItemTooltip("Wires and pins of buses and bundles as thick lines, otherwise only their colors differ");

        int names = (int)renderer.names;
        TextUnformatted("Instance names");
        SameLine();
        RadioButton("All", &names, (int)render::instance_names::all);
        SameLine();
        RadioButton("Custom", &names, (int)render::instance_names::custom);
        SetItemTooltip("Only names you gave, not the default U1, U2, ...");
        SameLine();
        RadioButton("None", &names, (int)render::instance_names::none);
        renderer.names = (render::instance_names)names;

        SeparatorText("Component bar");
        int bar = (int)components.dir;
        RadioButton("Vertical", &bar, (int)component_bar::orientation::vertical);
        SameLine();
        RadioButton("Horizontal", &bar, (int)component_bar::orientation::horizontal);
        set_bar_orientation((component_bar::orientation)bar);
    }
    End();
}

void app::draw_gui()
{
    using namespace ImGui;

    menu_bar();
    auto dockspace = DockSpaceOverViewport();
    default_layout(dockspace);
    // first start with an older imgui.ini: the bar has no place yet
    if (!FindWindowSettingsByID(ImHashStr("Components")) && !FindWindowByName("Components")) dock_bar = true;
    if (dock_bar) dock_component_bar();

    if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) save_all();

    // text inputs have their own undo
    if (auto e = find_editor(active_cell); e && !GetIO().WantTextInput) {
        if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) e->undo();
        if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) e->copy();
        if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_X)) e->cut();
        if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) e->paste();
        if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) || IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) e->redo();
    }
    if (auto b = find_bundle_editor(active_bundle); b && !GetIO().WantTextInput) {
        if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) b->undo();
        if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) || IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) b->redo();
    }
    if (IsKeyPressed(ImGuiKey_F1)) show_demo = !show_demo;
    if (show_demo) ShowDemoWindow(&show_demo);

    update_simulations();
    for (auto& m : plugins.take_messages())
        message(m);
    for (auto& m : hdl.take_messages())
        message(m);

    components_panel();
    library_panel();
    editor_panel();
    for (auto& e : editors)
        if (auto& s = e->simulation()) s->waveforms_gui(e->cell().name);
    open_instance_tabs();
    properties_panel();
    hierarchy_panel();
    messages_panel();
    settings_window();
    exit_dialog();
}

void app::default_layout(unsigned int dockspace)
{
    using namespace ImGui;

    // only when nothing is docked yet, e.g. first start without imgui.ini
    auto node = DockBuilderGetNode(dockspace);
    if (!node || node->IsSplitNode() || node->Windows.Size > 0) return;

    DockBuilderRemoveNodeChildNodes(dockspace);
    ImGuiID center = dockspace;
    ImGuiID left = DockBuilderSplitNode(center, ImGuiDir_Left, 0.15f, nullptr, &center);
    ImGuiID bar = DockBuilderSplitNode(center, ImGuiDir_Left, 0.14f, nullptr, &center);
    ImGuiID right = DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, nullptr, &center);
    ImGuiID bottom = DockBuilderSplitNode(center, ImGuiDir_Down, 0.15f, nullptr, &center);

    ImGuiID left_bottom = DockBuilderSplitNode(left, ImGuiDir_Down, 0.4f, nullptr, &left);

    DockBuilderDockWindow("Libraries", left);
    DockBuilderDockWindow("Hierarchy", left_bottom);
    DockBuilderDockWindow("Properties", right);
    DockBuilderDockWindow("Messages", bottom);
    DockBuilderDockWindow("Editor", center);
    DockBuilderDockWindow("Components", bar);
    DockBuilderFinish(dockspace);
    components.dir = component_bar::orientation::vertical;
}

void app::set_bar_orientation(component_bar::orientation o)
{
    if (components.dir == o) return;
    components.dir = o;
    dock_bar = true;
}

void app::dock_component_bar()
{
    using namespace ImGui;

    // vertical left of the editor, horizontal above it. if the editor isn't
    // docked the bar stays where it is.
    auto editor = FindWindowByName("Editor");
    if (!editor || !editor->DockNode) {
        dock_bar = editor == nullptr;
        return;
    }
    dock_bar = false;

    auto node = editor->DockNode;
    auto size = node->Size;
    auto& style = GetStyle();
    bool horizontal = components.dir == component_bar::orientation::horizontal;

    ImGuiID rest;
    ImGuiID bar;
    if (horizontal) {
        // one row of icons, its scrollbar and the tab
        float h = component_bar::icon_size + 2 * style.FramePadding.y + 2 * style.WindowPadding.y + style.ScrollbarSize + GetFrameHeight() + 4;
        bar = DockBuilderSplitNode(node->ID, ImGuiDir_Up, std::min(0.5f, h / std::max(size.y, 1.0f)), nullptr, &rest);
        DockBuilderSetNodeSize(bar, {size.x, h});
    } else {
        float w = 4 * (component_bar::icon_size + 2 * style.FramePadding.x + style.ItemSpacing.x) + 2 * style.WindowPadding.x + style.ScrollbarSize;
        bar = DockBuilderSplitNode(node->ID, ImGuiDir_Left, std::min(0.5f, w / std::max(size.x, 1.0f)), nullptr, &rest);
        DockBuilderSetNodeSize(bar, {w, size.y});
    }
    DockBuilderDockWindow("Components", bar);
    DockBuilderFinish(node->ID);
}

void app::components_panel()
{
    component_bar::target t;
    if (auto e = find_editor(active_cell); e && !active_instance_tab) {
        t.can_place = [e](model::id c) { return e->can_place(c); };
        t.placing = e->placing();
    }

    auto clicked = components.gui(ws, t);
    if (components.dir_changed) {
        components.dir_changed = false;
        dock_bar = true;
    }
    if (clicked != model::no_id) {
        if (auto e = find_editor(active_cell)) e->place(clicked);
        // the next click on the canvas places the instance
        ImGui::SetWindowFocus("Editor");
    }
}

void app::menu_bar()
{
    using namespace ImGui;

    bool open_help = false;

    if (BeginMainMenuBar()) {
        if (BeginMenu("File")) {
            if (auto work = ws.find_library("work")) new_cell_items(*work);
            if (MenuItem("Save all", "Ctrl+S", false, has_unsaved())) save_all();
            // cells change in place, not under a running simulation
            bool simulating = std::any_of(editors.begin(), editors.end(), [](auto& e) { return e->simulating(); });
            if (MenuItem("Reload plugins", nullptr, false, !simulating)) reload_plugins();
            SetItemTooltip("Finds new plugins and takes the ports, parameters and symbols of rebuilt ones");
            Separator();
            if (MenuItem("Exit")) {
                if (has_unsaved())
                    exit_requested = true;
                else
                    should_exit = true;
            }
            EndMenu();
        }

        if (BeginMenu("Edit")) {
            auto e = find_editor(active_cell);
            if (auto b = find_bundle_editor(active_bundle)) {
                if (MenuItem("Undo", "Ctrl+Z", false, b->can_undo())) b->undo();
                if (MenuItem("Redo", "Ctrl+Y", false, b->can_redo())) b->redo();
            } else {
                if (MenuItem("Undo", "Ctrl+Z", false, e && e->can_undo())) e->undo();
                if (MenuItem("Redo", "Ctrl+Y", false, e && e->can_redo())) e->redo();
            }
            Separator();
            if (MenuItem("Cut", "Ctrl+X", false, e && e->can_copy() && !e->locked())) e->cut();
            if (MenuItem("Copy", "Ctrl+C", false, e && e->can_copy())) e->copy();
            if (MenuItem("Paste", "Ctrl+V", false, e && e->can_paste())) e->paste();
            EndMenu();
        }

        if (BeginMenu("View")) {
            if (MenuItem("Settings...")) show_settings = true;
            if (BeginMenu("Waveforms")) {
                bool any = false;
                for (auto& e : editors) {
                    if (auto& s = e->simulation()) {
                        PushID((int)e->cell().id);
                        MenuItem(e->cell().name.c_str(), nullptr, &s->waveforms_visible);
                        PopID();
                        any = true;
                    }
                }
                if (!any) TextDisabled("Start a simulation with F5");
                EndMenu();
            }
            Separator();
            MenuItem("Grid", "G", &renderer.show_grid);
            if (BeginMenu("Grid style", renderer.show_grid)) {
                if (MenuItem("Dots", nullptr, renderer.grid == render::grid_style::dots)) renderer.grid = render::grid_style::dots;
                if (MenuItem("Lines", nullptr, renderer.grid == render::grid_style::lines)) renderer.grid = render::grid_style::lines;
                if (MenuItem("Checkers", nullptr, renderer.grid == render::grid_style::checkers)) renderer.grid = render::grid_style::checkers;
                EndMenu();
            }
            MenuItem("Fill symbols", nullptr, &renderer.fill_symbols);
            MenuItem("Thick buses and bundles", nullptr, &renderer.thick_buses);
            if (BeginMenu("Instance names")) {
                if (MenuItem("All", nullptr, renderer.names == render::instance_names::all)) renderer.names = render::instance_names::all;
                if (MenuItem("Custom only", nullptr, renderer.names == render::instance_names::custom)) renderer.names = render::instance_names::custom;
                SetItemTooltip("Only names you gave, not the default U1, U2, ...");
                if (MenuItem("None", nullptr, renderer.names == render::instance_names::none)) renderer.names = render::instance_names::none;
                EndMenu();
            }
            if (BeginMenu("Component bar")) {
                if (MenuItem("Vertical", nullptr, components.dir == component_bar::orientation::vertical)) set_bar_orientation(component_bar::orientation::vertical);
                if (MenuItem("Horizontal", nullptr, components.dir == component_bar::orientation::horizontal)) set_bar_orientation(component_bar::orientation::horizontal);
                EndMenu();
            }
            if (BeginMenu("Theme")) {
                if (MenuItem("Dark", nullptr, !light_theme)) {
                    light_theme = false;
                    apply_theme();
                }
                if (MenuItem("Light", nullptr, light_theme)) {
                    light_theme = true;
                    apply_theme();
                }
                EndMenu();
            }
            EndMenu();
        }

        if (BeginMenu("Help")) {
            if (MenuItem("Keys")) open_help = true;
            MenuItem("ImGui demo", "F1", &show_demo);
            EndMenu();
        }

        EndMainMenuBar();
    }

    if (open_help) OpenPopup("Keys");
    if (BeginPopupModal("Keys", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        SeparatorText("Both editors");
        BulletText("Wheel: zoom, right drag: pan, G: grid, Tab: schematic/symbol");
        BulletText("Click/drag: select and move, drag on empty space: box select, Shift: add");
        BulletText("Delete: delete selection, Esc: cancel tool / clear selection");
        BulletText("Shift+click while placing: keep placing");
        BulletText("Ctrl+Z: undo, Ctrl+Y / Ctrl+Shift+Z: redo, Ctrl+S: save all");
        BulletText("Ctrl+C / Ctrl+X / Ctrl+V: copy, cut, paste (click to place the copy)");
        SeparatorText("Schematic");
        BulletText("W: wires (click points, Esc ends the wire and goes back to select)");
        BulletText("I / O: input / output port, Space: place a cell");
        BulletText("Q: duplicate the element under the mouse and place it");
        BulletText("Ctrl+R / Ctrl+M: rotate / mirror the selection (Shift: the other way)");
        BulletText("R / M while placing or pasting: rotate / mirror it (Shift+R: the other way)");
        BulletText("Components bar: click an icon, then click in the schematic to place it");
        BulletText("Right click the components bar: vertical or horizontal");
        SeparatorText("Symbol");
        BulletText("P: pin, L: line, B: bezier, A: arc, C: circle, T: text, R: rectangle");
        SeparatorText("Simulate");
        BulletText("F5: start / stop simulating the schematic");
        BulletText("Click an input port: toggle it, wider inputs are set in the properties");
        BulletText("Space: step to the next event, e.g. a clock edge");
        BulletText("Run: simulated time passes at the set speed (1 x is real time) or as fast as possible");
        BulletText("Click an instance: its state and, for processors and memories, the debugger in the properties");
        BulletText("Buttons are pressed while held down, switches toggle on a click");
        BulletText("Double click an instance: look inside, Backspace: back to the parent");
        BulletText("Right click an instance: open it in a new tab");
        if (Button("Close")) CloseCurrentPopup();
        EndPopup();
    }
}

void app::library_panel()
{
    using namespace ImGui;

    Begin("Libraries", nullptr, ImGuiWindowFlags_NoCollapse);

    if (auto work = ws.find_library("work")) {
        if (Button("New cell")) new_cell(*work);
        SameLine(0, 2);
        if (ArrowButton("##new_kinds", ImGuiDir_Down)) OpenPopup("new_kinds");
        SetItemTooltip("C model and HDL cells");
        if (BeginPopup("new_kinds")) {
            new_cell_items(*work);
            EndPopup();
        }
        Separator();
    }

    for (auto& lib : ws.libraries) {
        bool plugin = !lib->symbols_dir.empty();
        auto label = lib->name + (lib->dirty ? " *" : "") + (plugin ? " (plugin)" : lib->read_only ? " (read only)" : "") + "###lib" + lib->name;
        bool open = TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

        if (!lib->read_only && BeginPopupContextItem()) {
            new_cell_items(*lib);
            EndPopup();
        }

        if (!open) continue;

        if (lib->cells.empty() && lib->bundles.empty()) TextDisabled("empty");

        // the categories as tree nodes, then the cells without one
        for (auto& cat : lib->categories()) {
            if (!TreeNodeEx(cat.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) continue;
            for (auto& c : lib->cells)
                if (c->category == cat) library_cell(*lib, *c);
            TreePop();
        }
        for (auto& c : lib->cells)
            if (c->category.empty()) library_cell(*lib, *c);

        library_bundles(*lib);
        TreePop();
    }

    // delete confirmation, refused while other cells use it
    if (deleting != model::no_id) OpenPopup("Delete cell");
    if (BeginPopupModal("Delete cell", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto c = ws.find_cell(deleting);
        auto users = ws.users_of(deleting);

        if (!c) {
            deleting = model::no_id;
            CloseCurrentPopup();
        } else if (!users.empty()) {
            Text("'%s' is used by:", c->name.c_str());
            for (auto u : users)
                BulletText("%s", u->name.c_str());
            TextUnformatted("Remove those instances first.");
            if (Button("Close")) {
                deleting = model::no_id;
                CloseCurrentPopup();
            }
        } else {
            Text("Delete '%s'? The file is removed on the next save.", c->name.c_str());
            if (Button("Delete")) {
                close_editor(deleting);
                ws.delete_cell(deleting);
                deleting = model::no_id;
                CloseCurrentPopup();
            }
            SameLine();
            if (Button("Cancel")) {
                deleting = model::no_id;
                CloseCurrentPopup();
            }
        }
        EndPopup();
    }

    if (deleting_bundle != model::no_id) OpenPopup("Delete bundle");
    if (BeginPopupModal("Delete bundle", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto b = ws.find_bundle(deleting_bundle);
        auto users = ws.bundle_users(deleting_bundle);

        if (!b) {
            deleting_bundle = model::no_id;
            CloseCurrentPopup();
        } else if (!users.empty()) {
            Text("'%s' is used by:", b->name.c_str());
            for (auto& u : users)
                BulletText("%s", u.c_str());
            TextUnformatted("Change those first.");
            if (Button("Close")) {
                deleting_bundle = model::no_id;
                CloseCurrentPopup();
            }
        } else {
            Text("Delete the bundle type '%s'? The file is removed on the next save.", b->name.c_str());
            if (Button("Delete")) {
                std::erase_if(bundle_editors, [&](auto& e) { return e->bundle().id == deleting_bundle; });
                ws.delete_bundle(deleting_bundle);
                deleting_bundle = model::no_id;
                CloseCurrentPopup();
            }
            SameLine();
            if (Button("Cancel")) {
                deleting_bundle = model::no_id;
                CloseCurrentPopup();
            }
        }
        EndPopup();
    }

    End();
}

void app::library_cell(model::library& lib, model::cell& c)
{
    using namespace ImGui;
    PushID((void*)&c);

    if (renaming == c.id) {
        rename_input(lib, c.name, false);
    } else {
        if (Selectable(c.name.c_str(), active_cell == c.id)) open_editor(c);

        if (!lib.read_only && IsItemHovered() && IsKeyPressed(ImGuiKey_F2)) start_rename(c.id, c.name);

        if (BeginPopupContextItem()) {
            if (Selectable("Open")) open_editor(c);
            if (!lib.read_only) {
                if (Selectable("Rename (F2)")) start_rename(c.id, c.name);
                if (BeginMenu("Category")) {
                    if (editor::edit_category(lib, c.category)) lib.dirty = true;
                    EndMenu();
                }
                if (Selectable("Delete")) deleting = c.id;
            }
            EndPopup();
        }
    }

    PopID();
}

void app::rename_input(model::library& lib, std::string& name, bool bundle)
{
    using namespace ImGui;

    if (rename_focus) {
        SetKeyboardFocusHere();
        rename_focus = false;
    }
    editor::input_string("##rename", rename_buf, ImGuiInputTextFlags_AutoSelectAll);

    if (IsItemDeactivated()) {
        bool valid = bundle ? model::workspace::valid_bundle_name(lib, rename_buf, rename_error) : model::workspace::valid_cell_name(lib, rename_buf, rename_error);
        if (IsKeyPressed(ImGuiKey_Escape) || rename_buf == name) {
            renaming = model::no_id;
        } else if (valid) {
            name = rename_buf;
            lib.dirty = true;
            renaming = model::no_id;
        } else {
            // keep editing until the name is valid or esc
            rename_focus = true;
        }
    }
    if (renaming != model::no_id && !rename_error.empty()) TextColored({1, 0.4f, 0.4f, 1}, "%s", rename_error.c_str());
}

void app::start_rename(model::id item, const std::string& name)
{
    renaming = item;
    rename_buf = name;
    rename_error.clear();
    rename_focus = true;
}

void app::library_bundles(model::library& lib)
{
    using namespace ImGui;

    if (lib.bundles.empty()) return;
    if (!TreeNodeEx("Bundles", ImGuiTreeNodeFlags_DefaultOpen)) return;
    SetItemTooltip("Bundle types: named wires that go together, for bundle ports and wires");

    for (auto& b : lib.bundles) {
        PushID((void*)b.get());
        if (renaming == b->id) {
            rename_input(lib, b->name, true);
        } else {
            if (Selectable(b->name.c_str(), active_bundle == b->id)) open_bundle_editor(lib, *b);
            if (!b->description.empty()) SetItemTooltip("%s", b->description.c_str());
            if (!lib.read_only && IsItemHovered() && IsKeyPressed(ImGuiKey_F2)) start_rename(b->id, b->name);

            if (BeginPopupContextItem()) {
                if (Selectable("Open")) open_bundle_editor(lib, *b);
                if (!lib.read_only) {
                    if (Selectable("Rename (F2)")) start_rename(b->id, b->name);
                    if (Selectable("Delete")) deleting_bundle = b->id;
                }
                EndPopup();
            }
        }
        PopID();
    }
    TreePop();
}

void app::open_bundle_editor(model::library& lib, model::bundle_type& b)
{
    select_tab = b.id;
    if (find_bundle_editor(b.id)) return;
    bundle_editors.push_back(std::make_unique<editor::bundle_editor>(ws, lib, b));
}

editor::bundle_editor* app::find_bundle_editor(model::id bundle)
{
    for (auto& e : bundle_editors)
        if (e->bundle().id == bundle) return e.get();
    return nullptr;
}

void app::new_bundle(model::library& lib)
{
    auto& b = ws.create_bundle(lib, lib.unique_bundle_name("bundle"));
    open_bundle_editor(lib, b);
    start_rename(b.id, b.name);
}

void app::editor_panel()
{
    using namespace ImGui;

    Begin("Editor", nullptr, ImGuiWindowFlags_NoCollapse);

    if (editors.empty() && instance_tabs.empty() && bundle_editors.empty()) {
        TextDisabled("Open a cell from the library panel, or create one with 'New cell'.");
    }

    active_cell = model::no_id;
    active_bundle = model::no_id;
    active_view = nullptr;
    active_instance_tab = nullptr;
    model::id close = model::no_id;
    model::id close_bundle = model::no_id;
    int close_instance = 0;

    if ((!editors.empty() || !instance_tabs.empty() || !bundle_editors.empty()) && BeginTabBar("editors", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs)) {
        for (auto& e : editors) {
            auto id = e->cell().id;
            // the ### part keeps the tab when the cell is renamed
            auto label = e->cell().name + "###" + std::to_string(id);

            ImGuiTabItemFlags flags = 0;
            if (select_tab == id) {
                flags |= ImGuiTabItemFlags_SetSelected;
                select_tab = model::no_id;
            }

            bool open = true;
            if (BeginTabItem(label.c_str(), &open, flags)) {
                active_cell = id;
                e->gui(editor_tex, editor_fb);
                active_view = e->simulation_view();
                EndTabItem();
            }
            if (!open) close = id;
        }

        // bundle types can't change under a running simulation
        bool simulating = std::any_of(editors.begin(), editors.end(), [](auto& e) { return e->simulating(); });
        for (auto& e : bundle_editors) {
            auto id = e->bundle().id;
            auto label = e->bundle().name + " (bundle)###bundle" + std::to_string(id);

            ImGuiTabItemFlags flags = 0;
            if (select_tab == id) {
                flags |= ImGuiTabItemFlags_SetSelected;
                select_tab = model::no_id;
            }

            bool open = true;
            if (BeginTabItem(label.c_str(), &open, flags)) {
                active_bundle = id;
                BeginChild("bundle");
                e->gui(simulating);
                EndChild();
                EndTabItem();
            }
            if (!open) close_bundle = id;
        }

        for (auto& t : instance_tabs) {
            // the top cell may be gone already, the tab closes next frame
            if (t.view->session().stopped) continue;
            auto label = t.view->title() + "###instance" + std::to_string(t.id);

            ImGuiTabItemFlags flags = 0;
            if (select_instance_tab == t.id) {
                flags |= ImGuiTabItemFlags_SetSelected;
                select_instance_tab = 0;
            }

            bool open = true;
            if (BeginTabItem(label.c_str(), &open, flags)) {
                // f5 in an instance tab stops its simulation
                if (IsKeyPressed(ImGuiKey_F5)) t.view->session().stopped = true;
                t.view->gui(editor_tex, editor_fb);
                active_view = active_instance_tab = t.view.get();
                EndTabItem();
            }
            if (!open) close_instance = t.id;
        }
        EndTabBar();
    }

    if (close != model::no_id) close_editor(close);
    if (close_bundle != model::no_id) std::erase_if(bundle_editors, [&](auto& e) { return e->bundle().id == close_bundle; });
    if (close_instance) std::erase_if(instance_tabs, [&](auto& t) { return t.id == close_instance; });

    End();
}

void app::properties_panel()
{
    ImGui::Begin("Properties");
    if (active_instance_tab)
        active_instance_tab->property_panel();
    else if (auto e = find_editor(active_cell))
        e->property_panel();
    ImGui::End();
}

void app::hierarchy_panel()
{
    using namespace ImGui;

    // layouts saved before this panel existed: dock it with the libraries
    auto lib = FindWindowByName("Libraries");
    if (lib && lib->DockId && !FindWindowSettingsByID(ImHashStr("Hierarchy"))) SetNextWindowDockID(lib->DockId);

    Begin("Hierarchy");
    if (active_view)
        active_view->hierarchy_panel();
    else
        TextWrapped("Instances of the simulated cell show up here. F5 starts a simulation.");
    End();
}

void app::update_simulations()
{
    // stopped from an instance tab, the editor might not be visible
    for (auto& e : editors)
        if (e->simulation() && e->simulation()->stopped) e->stop_simulation();
    std::erase_if(instance_tabs, [](auto& t) { return t.view->session().stopped; });

    for (auto& e : editors) {
        if (auto& s = e->simulation()) s->update();
    }

    // cells of a running simulation can't be edited elsewhere
    for (auto& e : editors) {
        std::string by;
        for (auto& other : editors) {
            auto& s = other->simulation();
            if (other != e && s && s->uses(e->cell().id)) by = other->cell().name;
        }
        e->set_locked_by(by);
    }
}

void app::open_instance_tabs()
{
    std::vector<editor::sim_view*> views;
    for (auto& e : editors)
        if (auto v = e->simulation_view()) views.push_back(v);
    for (auto& t : instance_tabs)
        views.push_back(t.view.get());

    for (auto v : views) {
        if (!v->open_in_new_tab) continue;
        auto view = std::make_unique<editor::sim_view>(ws, renderer, v->session_ptr(), *v->open_in_new_tab);
        v->open_in_new_tab.reset();
        select_instance_tab = next_instance_tab;
        instance_tabs.push_back({next_instance_tab++, std::move(view)});
    }
}

void app::messages_panel()
{
    using namespace ImGui;

    if (messages.empty()) return;

    Begin("Messages");
    if (Button("Clear")) messages.clear();
    {
        // compiler output lines up its markers by columns
        ui::mono_scope mono;
        for (auto& m : messages)
            TextWrapped("%s", m.c_str());
    }
    End();
}

void app::exit_dialog()
{
    using namespace ImGui;

    if (exit_requested) {
        OpenPopup("Unsaved changes");
        exit_requested = false;
    }

    if (BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        TextUnformatted("Save changes before exiting?");
        if (Button("Save")) {
            save_all();
            // stay open if saving failed, the error is in messages
            if (!has_unsaved()) should_exit = true;
            CloseCurrentPopup();
        }
        SameLine();
        if (Button("Discard")) {
            should_exit = true;
            CloseCurrentPopup();
        }
        SameLine();
        if (Button("Cancel")) CloseCurrentPopup();
        EndPopup();
    }
}

void app::open_editor(model::cell& c)
{
    select_tab = c.id;
    if (find_editor(c.id)) return;

    if (!c.get_schematic() && !c.is_external()) {
        message("'" + c.name + "' can't be edited");
        return;
    }

    auto lib = ws.library_of(c.id);
    editors.push_back(std::make_unique<editor::cell_editor>(editor::context{ws, *lib, c, renderer, &factory, &plugins, &hdl}));
}

void app::close_editor(model::id cell)
{
    std::erase_if(editors, [&](auto& e) { return e->cell().id == cell; });
}

editor::cell_editor* app::find_editor(model::id cell)
{
    for (auto& e : editors)
        if (e->cell().id == cell) return e.get();
    return nullptr;
}

void app::new_cell_items(model::library& lib)
{
    using namespace ImGui;
    if (MenuItem("New cell")) new_cell(lib);
    SetItemTooltip("A circuit drawn as a schematic");
    if (MenuItem("New C model cell")) new_cell(lib, model::c_model_impl{});
    SetItemTooltip("A part simulated by a model in a plugin, e.g. a microcontroller");
    if (MenuItem("New HDL cell")) new_cell(lib, model::hdl_impl{});
    SetItemTooltip("A part written in Verilog, SystemVerilog or VHDL");
    Separator();
    if (MenuItem("New bundle type")) new_bundle(lib);
    SetItemTooltip("Named wires that go together, like an interface in SystemVerilog");
}

void app::new_cell(model::library& lib, model::implementation impl)
{
    bool external = !std::holds_alternative<model::schematic>(impl);
    auto& c = ws.create_cell(lib, lib.unique_cell_name(external ? "model" : "cell"), std::move(impl));
    if (external) c.generate_symbol(model::pin_order::declaration);
    open_editor(c);

    start_rename(c.id, c.name);
}

void app::save_all()
{
    for (auto& lib : ws.libraries) {
        if (!lib->dirty) continue;
        try {
            // plugins: only their edited symbols
            if (lib->read_only) io::save_symbols(*lib);
            else io::save_library(*lib, root);
        } catch (std::exception& e) {
            message("saving library '" + lib->name + "' failed: " + e.what());
        }
    }
}

bool app::has_unsaved() const
{
    return std::any_of(ws.libraries.begin(), ws.libraries.end(), [](auto& l) { return l->dirty && (!l->read_only || !l->symbols_dir.empty()); });
}

void app::message(std::string m)
{
    std::fprintf(stderr, "%s\n", m.c_str());
    messages.push_back(std::move(m));
}

}
