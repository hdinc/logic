#include "app.h"
#include "../builtin/builtin.h"
#include "../io/storage.h"

#include <Hd/Resource.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace logic::app {

app::app(Hd::Window& w, std::filesystem::path r)
    : window(w)
    , gui(&w)
    , root(std::move(r))
    , factory({&builtin::factory(), &plugins})
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

    // stable order in the library panel
    std::stable_sort(ws.libraries.begin(), ws.libraries.end(), [](auto& a, auto& b) { return a->read_only < b->read_only; });
}

void app::run()
{
    auto f = Hd::Resource::getExeRes("res/fonts/Roboto.ttf");
    // imgui takes ownership of the font data
    void* font_data = IM_ALLOC(f.second);
    std::memcpy(font_data, f.first, f.second);
    ImGui::GetIO().Fonts->AddFontFromMemoryTTF(font_data, f.second, 20);

    apply_theme();
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

        gui.Draw();

        window.SwapBuffers();
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
            a->renderer.grid = std::strcmp(str, "lines") == 0 ? render::grid_style::lines : render::grid_style::dots;
        else if (std::sscanf(line, "ComponentBar=%31s", str) == 1)
            a->components.dir = std::strcmp(str, "horizontal") == 0 ? component_bar::orientation::horizontal : component_bar::orientation::vertical;
    };
    h.ApplyAllFn = [](ImGuiContext*, ImGuiSettingsHandler* h) {
        auto a = (app*)h->UserData;
        a->apply_theme();
        a->saved_settings = {a->light_theme, a->renderer.show_grid, a->renderer.grid, a->components.dir};
    };
    h.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* h, ImGuiTextBuffer* buf) {
        auto a = (app*)h->UserData;
        buf->appendf("[Logic][Settings]\nTheme=%s\nGrid=%d\nGridStyle=%s\nComponentBar=%s\n\n", a->light_theme ? "light" : "dark", a->renderer.show_grid ? 1 : 0,
            a->renderer.grid == render::grid_style::lines ? "lines" : "dots", a->components.dir == component_bar::orientation::horizontal ? "horizontal" : "vertical");
    };
    ImGui::AddSettingsHandler(&h);
    saved_settings = {light_theme, renderer.show_grid, renderer.grid, components.dir};
}

void app::apply_theme()
{
    render::set_theme(light_theme ? render::light_theme() : render::dark_theme());

    auto& s = ImGui::GetStyle();
    if (light_theme)
        ImGui::StyleColorsLight(&s);
    else
        ImGui::StyleColorsDark(&s);
    s.FrameRounding = 4.0f;
    s.GrabRounding = 4.0f;
    s.WindowRounding = 6.0f;
    s.PopupRounding = 4.0f;
    s.TabRounding = 4.0f;
}

void app::settings_window()
{
    using namespace ImGui;

    // also changed from the view menu and with G in the editors
    std::tuple current{light_theme, renderer.show_grid, renderer.grid, components.dir};
    if (current != saved_settings) {
        saved_settings = current;
        MarkIniSettingsDirty();
    }

    if (!show_settings) return;

    SetNextWindowSize({380, 0}, ImGuiCond_FirstUseEver);
    SetNextWindowPos(GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    if (Begin("Settings", &show_settings, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        SeparatorText("Appearance");
        int theme = light_theme ? 1 : 0;
        if (Combo("Theme", &theme, "Dark\0Light\0")) {
            light_theme = theme == 1;
            apply_theme();
        }

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
        renderer.grid = (render::grid_style)style;
        EndDisabled();

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
    if (IsKeyPressed(ImGuiKey_F1)) show_demo = !show_demo;
    if (show_demo) ShowDemoWindow(&show_demo);

    update_simulations();
    for (auto& m : plugins.take_messages())
        message(m);

    components_panel();
    library_panel();
    editor_panel();
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
            if (MenuItem("Undo", "Ctrl+Z", false, e && e->can_undo())) e->undo();
            if (MenuItem("Redo", "Ctrl+Y", false, e && e->can_redo())) e->redo();
            Separator();
            if (MenuItem("Cut", "Ctrl+X", false, e && e->can_copy() && !e->locked())) e->cut();
            if (MenuItem("Copy", "Ctrl+C", false, e && e->can_copy())) e->copy();
            if (MenuItem("Paste", "Ctrl+V", false, e && e->can_paste())) e->paste();
            EndMenu();
        }

        if (BeginMenu("View")) {
            if (MenuItem("Settings...")) show_settings = true;
            Separator();
            MenuItem("Grid", "G", &renderer.show_grid);
            if (BeginMenu("Grid style", renderer.show_grid)) {
                if (MenuItem("Dots", nullptr, renderer.grid == render::grid_style::dots)) renderer.grid = render::grid_style::dots;
                if (MenuItem("Lines", nullptr, renderer.grid == render::grid_style::lines)) renderer.grid = render::grid_style::lines;
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
        BulletText("I / O: input / output port, Space: place a cell, R: rotate");
        BulletText("Components bar: click an icon, then click in the schematic to place it");
        BulletText("Right click the components bar: vertical or horizontal");
        SeparatorText("Symbol");
        BulletText("P: pin, L: line, B: bezier, A: arc, C: circle, T: text");
        SeparatorText("Simulate");
        BulletText("F5: start / stop simulating the schematic");
        BulletText("Click an input port: toggle it, wider inputs are set in the properties");
        BulletText("Space: step to the next event, e.g. a clock edge");
        BulletText("Run: simulated time passes at the set speed (1 x is real time) or as fast as possible");
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
        auto label = lib->name + (lib->dirty ? " *" : "") + (lib->read_only ? " (read only)" : "") + "###lib" + lib->name;
        bool open = TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

        if (!lib->read_only && BeginPopupContextItem()) {
            new_cell_items(*lib);
            EndPopup();
        }

        if (!open) continue;

        if (lib->cells.empty()) TextDisabled("empty");

        for (auto& c : lib->cells) {
            PushID((void*)c.get());

            if (renaming == c->id) {
                if (rename_focus) {
                    SetKeyboardFocusHere();
                    rename_focus = false;
                }
                editor::input_string("##rename", rename_buf, ImGuiInputTextFlags_AutoSelectAll);

                if (IsItemDeactivated()) {
                    if (IsKeyPressed(ImGuiKey_Escape) || rename_buf == c->name) {
                        renaming = model::no_id;
                    } else if (model::workspace::valid_cell_name(*lib, rename_buf, rename_error)) {
                        c->name = rename_buf;
                        lib->dirty = true;
                        renaming = model::no_id;
                    } else {
                        // keep editing until the name is valid or esc
                        rename_focus = true;
                    }
                }
                if (renaming == c->id && !rename_error.empty()) TextColored({1, 0.4f, 0.4f, 1}, "%s", rename_error.c_str());
            } else {
                if (Selectable(c->name.c_str(), active_cell == c->id)) open_editor(*c);

                if (!lib->read_only && IsItemHovered() && IsKeyPressed(ImGuiKey_F2)) {
                    renaming = c->id;
                    rename_buf = c->name;
                    rename_error.clear();
                    rename_focus = true;
                }

                if (BeginPopupContextItem()) {
                    if (Selectable("Open")) open_editor(*c);
                    if (!lib->read_only) {
                        if (Selectable("Rename (F2)")) {
                            renaming = c->id;
                            rename_buf = c->name;
                            rename_error.clear();
                            rename_focus = true;
                        }
                        if (Selectable("Delete")) deleting = c->id;
                    }
                    EndPopup();
                }
            }

            PopID();
        }

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

    End();
}

void app::editor_panel()
{
    using namespace ImGui;

    Begin("Editor", nullptr, ImGuiWindowFlags_NoCollapse);

    if (editors.empty() && instance_tabs.empty()) {
        TextDisabled("Open a cell from the library panel, or create one with 'New cell'.");
    }

    active_cell = model::no_id;
    active_view = nullptr;
    active_instance_tab = nullptr;
    model::id close = model::no_id;
    int close_instance = 0;

    if ((!editors.empty() || !instance_tabs.empty()) && BeginTabBar("editors", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs)) {
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
    for (auto& m : messages)
        TextWrapped("%s", m.c_str());
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
    editors.push_back(std::make_unique<editor::cell_editor>(editor::context{ws, *lib, c, renderer, &factory, &plugins}));
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
}

void app::new_cell(model::library& lib, model::implementation impl)
{
    bool external = !std::holds_alternative<model::schematic>(impl);
    auto& c = ws.create_cell(lib, lib.unique_cell_name(external ? "model" : "cell"), std::move(impl));
    if (external) c.generate_symbol(model::pin_order::declaration);
    open_editor(c);

    renaming = c.id;
    rename_buf = c.name;
    rename_error.clear();
    rename_focus = true;
}

void app::save_all()
{
    for (auto& lib : ws.libraries) {
        if (!lib->dirty || lib->read_only) continue;
        try {
            io::save_library(*lib, root);
        } catch (std::exception& e) {
            message("saving library '" + lib->name + "' failed: " + e.what());
        }
    }
}

bool app::has_unsaved() const
{
    return std::any_of(ws.libraries.begin(), ws.libraries.end(), [](auto& l) { return l->dirty && !l->read_only; });
}

void app::message(std::string m)
{
    std::fprintf(stderr, "%s\n", m.c_str());
    messages.push_back(std::move(m));
}

}
