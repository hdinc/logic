#include "app.h"
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
    , editor_tex(Hd::Texture::emptyTexture(3840, 2160))
    , editor_fb(editor_tex)
{
    for (auto& e : io::load_workspace(ws, root))
        message(e);

    if (!ws.find_library("work")) ws.add_library("work");

    // TODO: builtin gates are defined in code once the simulator exists
    auto& builtin = ws.add_library("builtin");
    builtin.read_only = true;

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

    ImGui::StyleColorsDark();
    auto& s = ImGui::GetStyle();
    s.FrameRounding = 4.0f;
    s.GrabRounding = 4.0f;
    s.WindowRounding = 6.0f;

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

void app::draw_gui()
{
    using namespace ImGui;

    menu_bar();
    auto dockspace = DockSpaceOverViewport();
    default_layout(dockspace);

    if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) save_all();

    // text inputs have their own undo
    if (auto e = find_editor(active_cell); e && !GetIO().WantTextInput) {
        if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) e->undo();
        if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) || IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) e->redo();
    }
    if (IsKeyPressed(ImGuiKey_F1)) show_demo = !show_demo;
    if (show_demo) ShowDemoWindow(&show_demo);

    library_panel();
    editor_panel();
    properties_panel();
    messages_panel();
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
    ImGuiID right = DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, nullptr, &center);
    ImGuiID bottom = DockBuilderSplitNode(center, ImGuiDir_Down, 0.15f, nullptr, &center);

    DockBuilderDockWindow("Libraries", left);
    DockBuilderDockWindow("Properties", right);
    DockBuilderDockWindow("Messages", bottom);
    DockBuilderDockWindow("Editor", center);
    DockBuilderFinish(dockspace);
}

void app::menu_bar()
{
    using namespace ImGui;

    bool open_help = false;

    if (BeginMainMenuBar()) {
        if (BeginMenu("File")) {
            if (MenuItem("New cell", nullptr, false, ws.find_library("work") != nullptr)) new_cell(*ws.find_library("work"));
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
        SeparatorText("Schematic");
        BulletText("W: wires (click points, Esc ends the wire, Esc again leaves the tool)");
        BulletText("I / O: input / output port, Space: place a cell, R: rotate");
        SeparatorText("Symbol");
        BulletText("P: pin, L: line, B: bezier, C: circle, T: text");
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
        Separator();
    }

    for (auto& lib : ws.libraries) {
        auto label = lib->name + (lib->dirty ? " *" : "") + (lib->read_only ? " (read only)" : "") + "###lib" + lib->name;
        bool open = TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

        if (!lib->read_only && BeginPopupContextItem()) {
            if (Selectable("New cell")) new_cell(*lib);
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

    if (editors.empty()) {
        TextDisabled("Open a cell from the library panel, or create one with 'New cell'.");
    }

    active_cell = model::no_id;
    model::id close = model::no_id;

    if (!editors.empty() && BeginTabBar("editors", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs)) {
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
                EndTabItem();
            }
            if (!open) close = id;
        }
        EndTabBar();
    }

    if (close != model::no_id) close_editor(close);

    End();
}

void app::properties_panel()
{
    ImGui::Begin("Properties");
    if (auto e = find_editor(active_cell)) e->property_panel();
    ImGui::End();
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

    if (!c.get_schematic()) {
        message("'" + c.name + "' is not a schematic and can't be edited");
        return;
    }

    auto lib = ws.library_of(c.id);
    editors.push_back(std::make_unique<editor::cell_editor>(editor::context{ws, *lib, c, renderer}));
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

void app::new_cell(model::library& lib)
{
    auto& c = ws.create_cell(lib, lib.unique_cell_name("cell"));
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
