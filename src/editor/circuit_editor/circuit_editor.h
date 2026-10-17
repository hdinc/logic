#pragma once

#include "../editor.h"
#include "../symbol.h"

#include "component.h"
#include "wire.h"
#include "port.h"

#include <unordered_set>
#include <unordered_map>
#include "../../library/library.h"

namespace logic::editor::circuit_editor {

class circuit_editor : public editor {
public:
    circuit_editor(std::string library_name, std::string name, std::unordered_map<std::string, library::library>& libs);
    void process_inputs();
    void draw(Hd::FrameBuffer* fb, int sx, int sy);
    void property_editor();
    void load_circuit();
    void reload_symbols();
    void add_component(std::string library_name, std::string name);
    symbol::symbol* load_symbol(std::string library_name, std::string name);

    std::vector<library::wire> get_wires();
    std::vector<library::component> get_components();
    std::vector<library::port> get_ports();

public:
    std::string library_name;
    std::string name;

private:
    enum class input_state {
        idle,

        add_component,
        add_port,
        add_wire1,
        add_wire2,

        select1,
        select2,

        move0,
        move1,
    };

    input_state istate = input_state::idle;

    wire_point temp_wire_p1;
    wire_point temp_wire_p2;

    wire temp_wire;

    glm::vec2 select1_pos;
    glm::vec2 select2_pos;

private:
    std::unordered_map<item*, std::unique_ptr<item>> items;
    std::unordered_map<wire*, std::unique_ptr<wire>> wires;

    std::unordered_set<component*> components;
    std::unordered_set<port*> ports;

    std::vector<item*> selected_items;

    glm::vec2 move_start;
    glm::vec2 move_end;

    // bool auto_gen_symbol = true;
    // int auto_gen_symbol_width = 5;
    // symbol::symbol symbol;

    std::unordered_map<std::string, library::library>& libraries;

    std::unordered_map<std::string, std::unique_ptr<symbol::symbol>> symbols;

    std::unique_ptr<component> temp_component;
    std::unique_ptr<port> temp_port;

    void reduce_wires();

    bool _draw_grid = true;

};
}
