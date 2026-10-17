#pragma once

#include "../../library/library.h"
#include "../editor.h"
#include "../symbol.h"

#include "pin.h"
#include "line.h"
#include "bezier.h"
#include "text.h"
#include "circle.h"

#include <unordered_set>

namespace logic::editor::symbol_editor {

class symbol_editor : public editor {
public:
    symbol_editor(std::string library_name, std::string name, std::unordered_map<std::string, library::library>& libs);
    void process_inputs();
    void draw(Hd::FrameBuffer* fb, int sx, int sy);
    void property_editor();

    symbol::symbol get_symbol();
    void load_symbol();

public:
    std::string library_name;
    std::string name;

private:
    enum class input_state {
        idle,

        add_pin,

        add_line1,
        add_line2,

        add_bezier1,
        add_bezier2,
        add_bezier3,

        add_text,

        add_circle1,
        add_circle2,

        select1,
        select2,

        move0,
        move1,
    };

    input_state istate = input_state::idle;

    control_point cp1;
    control_point cp2;
    control_point cp3;

    pin temp_pin;
    line temp_line;
    bezier temp_bezier;
    text temp_text;
    circle temp_circle;

    glm::vec2 select1_pos;
    glm::vec2 select2_pos;

    std::unordered_map<item*, std::unique_ptr<item>> items;

    std::vector<item*> selected_items;

    std::unordered_set<std::unique_ptr<line>> lines;
    std::unordered_set<std::unique_ptr<bezier>> beziers;
    std::unordered_set<std::unique_ptr<circle>> circles;

    std::unordered_set<text*> texts;
    std::unordered_set<pin*> pins;

    glm::vec2 move_start;
    glm::vec2 move_end;
    bool move_snap;
    float move_dist;

    std::unordered_map<std::string, library::library>& libraries;
};
}