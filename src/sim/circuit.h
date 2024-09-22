#pragma once

#include <vector>
#include "gate.h"
#include "../draw/draw.h"
#include "../draw/grid.h"
#include "wire.h"
#include <set>


class circuit {
public:

    circuit(Hd::Camera& c);
    ~circuit();

    circuit(const circuit&) = delete;
    circuit& operator=(const circuit&) = delete;

    void single_step();
    void step();

    void draw();

    void gui();

    void process_inputs();

    void select_gate(glm::vec2 loc);
    // pin* select_pin(glm::vec2 loc);

    // gate* selected_gate = 0;
    glm::vec2 select_diff;

    std::vector<gate*> gates;

    std::vector<wire> wires;

    std::vector<net> nets;

    // std::vector<gate*> ins;
    // std::vector<gate*> outs;

    // std::vector<pin> pins;

    std::set<glm::ivec2*> selection;
    // std::vector<wire2::segment*> selected_segments;
    // std::vector<wire2::segment> copied_segments;

    // bool settled = 0;
    glm::vec2 mouse_press_loc_world;

    drawer d;
    grid g;
    Hd::Camera& cam;

    // void delete_gate(gate* g);

    // void delete_wire(wire2* w);

    // void check_gate_pos(gate* g);

    // void remove_connected_wire_form_pin(pin* p);
    // void connect_wire_to_pin(pin* p , wire2* w);

    // void add_segment(wire2::segment& s);

    void add_wire(glm::ivec2 p1, glm::ivec2 p2);

    void box_select(glm::vec2 p1, glm::vec2 p2);

    void update_pins();

    void create_nets();

    // void draw_as_gate();

private:
    enum class mode { sim, edit };

    glm::vec2 to_world_coord(glm::vec2 p);
    glm::vec2 get_mouse_world_pos();

    mode mode = mode::edit;

    void reduce_wires();


};
