#pragma once

#include "../editor/cell_editor.h"
#include "../model/workspace.h"
#include "../render/renderer.h"

#include <Hd/FrameBuffer.h>
#include <Hd/Gui.h>
#include <Hd/Texture.h>
#include <Hd/Window.h>

#include <filesystem>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace logic::app {

class app {
public:
    // loads the workspace from root, libraries are saved back there
    app(Hd::Window& window, std::filesystem::path root);

    void run();

private:
    void draw_gui();
    void default_layout(unsigned int dockspace);
    void menu_bar();
    void library_panel();
    void editor_panel();
    void properties_panel();
    void hierarchy_panel();
    void messages_panel();
    void update_simulations();
    void open_instance_tabs();
    void exit_dialog();
    void settings_window();

    // theme and canvas options, kept in imgui.ini
    void add_settings_handler();
    void apply_theme();

    void open_editor(model::cell& c);
    void close_editor(model::id cell);
    editor::cell_editor* find_editor(model::id cell);

    void new_cell(model::library& lib);
    void save_all();
    bool has_unsaved() const;

    void message(std::string m);

    Hd::Window& window;
    Hd::Gui gui;
    std::filesystem::path root;

    model::workspace ws;
    render::renderer renderer;

    // all editors draw into this, only the visible one each frame
    Hd::Texture editor_tex;
    Hd::FrameBuffer editor_fb;

    std::vector<std::unique_ptr<editor::cell_editor>> editors;
    model::id active_cell = model::no_id;
    model::id select_tab = model::no_id;

    // instances of running simulations opened in their own tab
    struct instance_tab {
        int id;
        std::unique_ptr<editor::sim_view> view;
    };
    std::vector<instance_tab> instance_tabs;
    int next_instance_tab = 1;
    int select_instance_tab = 0;
    // the sim view in the active tab: an instance tab or a simulating editor
    editor::sim_view* active_view = nullptr;
    editor::sim_view* active_instance_tab = nullptr;

    model::id renaming = model::no_id;
    std::string rename_buf;
    std::string rename_error;
    bool rename_focus = false;

    model::id deleting = model::no_id;

    bool exit_requested = false;
    bool should_exit = false;
    bool show_demo = false;
    bool show_settings = false;

    bool light_theme = false;
    // settings at the last check, a change marks imgui.ini for saving
    std::tuple<bool, bool, render::grid_style> saved_settings;

    std::vector<std::string> messages;
};

}
