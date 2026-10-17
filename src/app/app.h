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
    void messages_panel();
    void exit_dialog();

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

    model::id renaming = model::no_id;
    std::string rename_buf;
    std::string rename_error;
    bool rename_focus = false;

    model::id deleting = model::no_id;

    bool exit_requested = false;
    bool should_exit = false;
    bool show_demo = false;

    std::vector<std::string> messages;
};

}
