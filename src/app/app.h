#pragma once

#include "component_bar.h"
#include "theme_editor.h"
#include "../editor/bundle_editor.h"
#include "../editor/cell_editor.h"
#include "../model/workspace.h"
#include "../hdl/manager.h"
#include "../plugin/registry.h"
#include "../render/renderer.h"
#include "../ui/fonts.h"

#include <Hd/FrameBuffer.h>
#include <Hd/Gui.h>
#include <Hd/Texture.h>
#include <Hd/Window.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace logic::app {

class app {
public:
    // loads the workspace from root, libraries are saved back there
    app(Hd::Window& window, std::filesystem::path root);
    ~app();

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
    void components_panel();
    // docks the component bar next to the editor for its orientation
    void dock_component_bar();
    void set_bar_orientation(component_bar::orientation o);

    // theme and canvas options, kept in imgui.ini
    void add_settings_handler();
    // loads the theme named theme_name and shows it
    void apply_theme();
    // shows ui::current() in imgui and on the canvas
    void use_theme();
    // imgui's fonts, between frames
    void update_font();
    void update_canvas_font();

    void open_editor(model::cell& c);
    void close_editor(model::id cell);
    editor::cell_editor* find_editor(model::id cell);

    // bundle types are edited in tabs next to the cells
    void open_bundle_editor(model::library& lib, model::bundle_type& b);
    editor::bundle_editor* find_bundle_editor(model::id bundle);
    // a cell in the library panel: open, rename, category, delete
    void library_cell(model::library& lib, model::cell& c);
    // the bundles of a library in the library panel
    void library_bundles(model::library& lib);
    void new_bundle(model::library& lib);
    // the name of a cell or bundle being renamed in the library panel
    void rename_input(model::library& lib, std::string& name, bool bundle);
    void start_rename(model::id item, const std::string& name);

    // a schematic cell by default, opened and renamed right away
    void new_cell(model::library& lib, model::implementation impl = model::schematic{});
    // "New ..." items for the kinds of cells
    void new_cell_items(model::library& lib);
    // the plugins' libraries, see plugin::sync_libraries
    void reload_plugins();
    void save_all();
    bool has_unsaved() const;

    void message(std::string m);

    Hd::Window& window;
    Hd::Gui gui;
    std::filesystem::path root;

    model::workspace ws;
    // c model plugins, the provider of their interfaces. outlives the
    // editors and their simulations.
    plugin::registry plugins{ws};
    // hdl cells, builds cached in <workspace>/.cache
    hdl::manager hdl{ws, plugins, root / ".cache"};
    sim::factory_chain factory;
    render::renderer renderer;
    component_bar components;
    // dock the bar before the windows are drawn next frame
    bool dock_bar = false;

    // all editors draw into this, only the visible one each frame
    Hd::Texture editor_tex;
    Hd::FrameBuffer editor_fb;

    std::vector<std::unique_ptr<editor::cell_editor>> editors;
    model::id active_cell = model::no_id;
    model::id select_tab = model::no_id;

    std::vector<std::unique_ptr<editor::bundle_editor>> bundle_editors;
    model::id active_bundle = model::no_id;

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

    // a cell or a bundle type
    model::id renaming = model::no_id;
    std::string rename_buf;
    std::string rename_error;
    bool rename_focus = false;

    model::id deleting = model::no_id;
    model::id deleting_bundle = model::no_id;

    bool exit_requested = false;
    bool should_exit = false;
    bool show_demo = false;
    bool show_settings = false;

    // a built-in theme ("dark", "light", "black") or a custom one, see
    // ui::find_theme
    std::string theme_name = "dark";
    theme_editor themes;
    int font_size = 20;
    // interface, monospace (debugger, terminal, messages) and canvas fonts,
    // the built-in ones if they have no file
    ui::font_ref ui_font;
    ui::font_ref mono_font;
    ui::font_ref canvas_font;
    // a new choice of fonts is loaded at the next frame boundary
    bool font_update_pending = true;
    bool canvas_font_pending = false;
    // installed fonts, found when the settings window opens
    std::optional<std::vector<ui::font_face>> system_fonts;
    ui::font_picker ui_font_picker;
    ui::font_picker mono_font_picker{true};
    ui::font_picker canvas_font_picker;
    // settings at the last check, a change marks imgui.ini for saving
    using settings = std::tuple<std::string, bool, bool, render::grid_style, render::instance_names, bool, bool, component_bar::orientation, component_bar::icon_size, int, ui::font_ref, ui::font_ref, ui::font_ref>;
    settings current_settings() const;
    settings saved_settings;

    std::vector<std::string> messages;
};

}
