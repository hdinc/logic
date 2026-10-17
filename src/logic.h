#include <Hd.h>

#include "Hd/FrameBuffer.h"
#include <string>
#include "editor/circuit_editor/circuit_editor.h"
#include "editor/symbol_editor/symbol_editor.h"

#pragma once

namespace logic {

class logic {
public:
    logic(Hd::Window* w);
    ~logic();
    void exec();

    struct editor_data {
        editor_data(std::string library_name, std::string name, std::unordered_map<std::string, library::library>& libs)
            : ce(library_name, name, libs)
            , se(library_name, name, libs)
        {
            this->library_name = library_name;
            this->name = name;
        }
        std::string library_name;
        std::string name;
        editor::circuit_editor::circuit_editor ce;
        editor::symbol_editor::symbol_editor se;
    };

private:
    void process_inputs();
    void draw();
    void draw_gui();
    void save_editor(editor_data& e);

private:
    // struct circuit_data {
    //     bool rename = false;
    //     bool rename_activated = false;
    // };

    Hd::Texture editor_tex;
    Hd::FrameBuffer editor_fb;
    std::unordered_map<std::string, std::unique_ptr<editor_data>> editors;
    // editor::circuit_editor::circuit_editor* selected_circuit_editor = 0;

    bool show_demo = false;

    bool show_symbol_editor = false;

    Hd::Window* window = 0;
    Hd::Gui gui;

private:
    std::unordered_map<std::string, library::library> libraries;
};

}
