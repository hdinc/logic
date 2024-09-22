#include "circuit.h"
#include <imgui.h>
#include "gates/and_gate.h"
#include "gates/nand_gate.h"
#include "gates/or_gate.h"
#include "gates/input_gate.h"
#include "gates/output_gate.h"
#include <algorithm>
#include <string>
#include <memory>
#include <stdexcept>
#include "../misc.h"
#include <queue>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <glm/gtx/hash.hpp>
#include <algorithm>

struct segment {
    glm::ivec2 p1;
    glm::ivec2 p2;
};

static segment temp_segment;

enum class input_state {idle, drag, wiring, select, drag_wires};

std::map<input_state, const char*> input_state_str {
    {input_state::idle, "idle"},
    {input_state::drag, "drag"},
    {input_state::wiring, "wiring"},
    {input_state::select, "select"},
    {input_state::drag_wires, "drag_wires"},
};

static input_state istate = input_state::idle;

void circuit::add_wire(glm::ivec2 p1, glm::ivec2 p2) {
    if (p1 == p2) return;

    wires.emplace_back(p1, p2);
    reduce_wires();
}

void circuit::reduce_wires() {

    std::unordered_set<glm::ivec2> hori_points;
    std::unordered_set<glm::ivec2> vert_points;
    std::unordered_set<glm::ivec2> diag_points;

    std::vector<wire> new_wires;

    struct swire {
        swire(int c, int p1, int p2) {
            this->c = c;
            if (p2 < p1) {
                this->p1 = p2;
                this->p2 = p1;
            } else {
                this->p1 = p1;
                this->p2 = p2;
            }
        }

        bool try_merge(const swire& other) {
            if (p2 < other.p1 || other.p2 < p1) {
                return false;
            } else {
                p1 = std::min(p1, other.p1);
                p2 = std::max(p2, other.p2);
                return true;
            }
        }

        bool inside(int p) {
            return p1 < p && p2 > p;
        }

        int c;
        int p1;
        int p2;
    };

    struct swire_container {

        swire_container(int c) {
            this->c = c;
        }

        void add(swire w) {

            wires.push_back(w);

            loop:
            for (int i = 0; i < wires.size() - 1; i++) {
                if (wires[i].try_merge(wires.back()))  {
                    std::iter_swap(wires.begin() + i, wires.end() - 2);
                    wires.pop_back();
                    goto loop;
                }
            }
        }

        void protect(int p) {
            for (int i = 0; i < wires.size(); i++) {
                swire& sw = wires[i];
                if (wires[i].inside(p)) {
                    int p2 = sw.p2;
                    sw.p2 = p;
                    wires.emplace_back(c, p, p2);
                }
            }
        }

        int c;
        std::vector<swire> wires;
    };

    std::unordered_map<int, swire_container> hori;
    std::unordered_map<int, swire_container> vert;

    for (wire& w : wires) {
        if (w.p1 == w.p2) {
            continue;
        } else {
            if (w.p1.y == w.p2.y) { // horizontal

                swire sw(w.p1.y, w.p1.x, w.p2.x);

                auto c = hori.find(w.p1.y);

                if (c == hori.end()) {
                    auto r = hori.emplace(w.p1.y, swire_container{w.p1.y});
                    c = r.first;
                }

                c->second.add(sw);

                hori_points.insert(w.p1);
                hori_points.insert(w.p2);
            } else
            if (w.p1.x == w.p2.x) { // vertical

                swire sw(w.p1.x, w.p1.y, w.p2.y);

                auto c = vert.find(w.p1.x);

                if (c == vert.end()) {
                    auto r = vert.emplace(w.p1.x, swire_container{w.p1.x});
                    c = r.first;
                }

                c->second.add(sw);

                vert_points.insert(w.p1);
                vert_points.insert(w.p2);

            } else
            {

                if (std::find(new_wires.begin(), new_wires.end(), w) == new_wires.end()) {
                    new_wires.push_back(w);
                    diag_points.insert(w.p1);
                    diag_points.insert(w.p2);
                }

            }
        }
    }

    for (const glm::ivec2& v : diag_points) {
        auto wc = hori.find(v.y);
        if (wc != hori.end()) {
            (wc->second).protect(v.x);
        }
    }

    for (const glm::ivec2& v : vert_points) {
        auto wc = hori.find(v.y);
        if (wc != hori.end()) {
            (wc->second).protect(v.x);
        }
    }

    for (const glm::ivec2& v : diag_points) {
        auto wc = vert.find(v.x);
        if (wc != vert.end()) {
            (wc->second).protect(v.y);
        }
    }

    for (const glm::ivec2& v : hori_points) {
        auto wc = vert.find(v.x);
        if (wc != vert.end()) {
            (wc->second).protect(v.y);
        }
    }

    for (auto it : hori) {
        for (swire& sw : it.second.wires) {
            new_wires.emplace_back(glm::ivec2{sw.p1, sw.c}, glm::ivec2{sw.p2, sw.c});
        }
    }

    for (auto it : vert) {
        for (swire& sw : it.second.wires) {
            new_wires.emplace_back(glm::ivec2{sw.c, sw.p1}, glm::ivec2{sw.c, sw.p2});
        }
    }

    wires = std::move(new_wires);

}

void circuit::draw() {
    g.draw();

    for (wire& w : wires) {
        w.draw(d);
    }

    for( gate* i : gates) {
        i->draw(d);
    }

    if (istate == input_state::wiring) {
        d.set_color(1, 1, 1, 1);
        d.line(temp_segment.p1, temp_segment.p2);
    }

    if (istate == input_state::select) {
        auto io = ImGui::GetIO();
        glm::vec2 p1 = mouse_press_loc_world;
        glm::vec2 p2 = get_mouse_world_pos();
        d.set_color(0.7, 0.2, 0.2, 1);
        d.rect((p1+p2)/2.0f, glm::abs((p1-p2)/2.0f), false);
    }

    // std::string s = "hello\nworld\n1234567890";
    // auto size = d.get_text_size(s, 1);
    // d.text(s, {0,0}, 1);
    // d.set_color(1,1,1,1);
    // d.rect((size.first + size.second)*0.5f, (size.second - size.first)*0.5f, false);

}

glm::vec2 circuit::get_mouse_world_pos() {
    auto io = ImGui::GetIO();
    return to_world_coord({io.MousePos.x, io.MousePos.y});
}

glm::vec2 circuit::to_world_coord(glm::vec2 p) {
    auto io = ImGui::GetIO();
    glm::vec2 mloc = glm::vec2{io.MousePos.x / io.DisplaySize.x, io.MousePos.y / io.DisplaySize.y};
    mloc = (mloc - glm::vec2{0.5, 0.5}) * 2.0f;
    mloc.y = -mloc.y;
    mloc = glm::inverse(*cam.getVP()) * glm::vec4(mloc.x, mloc.y, 0, 1);
    return mloc;
}

auto imgui_hovered = []() { return ImGui::IsAnyItemHovered() || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow); };

static glm::vec2 snap_to_grid(glm::vec2 l) {
    return {std::round(l.x), std::round(l.y)};
}

static bool check_overlap_in_grid(glm::vec2 a, glm::vec2 b) {
    auto d = a - b;
    d = glm::abs(d);
    return d.x < 0.1 && d.y < 0.1;
}


circuit::circuit(Hd::Camera& c) : cam(c), d(c), g(d) {
}

circuit::~circuit() {
    for (gate* i : gates) {
        delete i;
    }

    // for (wire2* i : wires) {
    //     delete i;
    // }
}

void circuit::step() {
    // do {
    //     single_step();
    // } while (!settled);
}

void circuit::single_step() {
    // settled = true;
    // for (gate* i : gates) {
    //     if (i->need_update) {
    //         settled = false;
    //         i->update();
    //     }
    // }
}

void circuit::process_inputs() {
    auto io = ImGui::GetIO();

    glm::vec2 mwloc = get_mouse_world_pos();
    glm::vec2 mloc = {io.MousePos.x, io.MousePos.y};

    static glm::vec2 mouse_press_loc {0,0};
    static glm::ivec2 dragpos;

    pin* p;

    switch (istate) {

        case input_state::idle:
        {

            // TODO: efficient delete, swap with end and remove from end without preserving order
            // if(selected_gate) {
            //     if (ImGui::IsKeyPressed(ImGuiKey_X)) {
            //         delete_gate(selected_gate);
            //         selected_gate = 0;
            //     }
            // }

            if (io.MouseClicked[0] && !imgui_hovered()) {
                mouse_press_loc_world = mwloc;
                mouse_press_loc = mloc;

                // p = select_pin(mwloc);
                // select_gate(mwloc);

                // if (selected_gate) {
                //     istate = input_state::drag;
                //     dragpos = selected_gate->pos;
                // }

            }

            if (io.MouseDown[0] && !imgui_hovered()) {
                if (glm::length(mouse_press_loc - mloc) > 10) {
                    istate = input_state::select;
                }
            }

            if (io.MouseReleased[0] && !imgui_hovered()) {
                istate = input_state::wiring;
                // if (!selected_gate || p) {

                    temp_segment.p1 = snap_to_grid(mwloc);
                    temp_segment.p2 = snap_to_grid(mwloc);

                    istate = input_state::wiring;
                // }
            }

            if (1) {
            }


            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_V)) {
                // if (copied_segments.size() > 0) {
                //     for (auto& s : copied_segments) {
                //         add_segment(s);
                //     }
                // }
            }

            break;
        }

        case input_state::drag:
        {

            glm::vec2 l = mwloc - select_diff;
            float gs = 1.0f;
            // selected_gate->pos = {std::round(gs*l.x)/gs, std::round(gs*l.y)/gs};

            if(io.MouseReleased[0]) {
                istate = input_state::idle;
                // glm::vec2 d = dragpos - selected_gate->pos;
                // d = glm::vec2(glm::abs(d.x), glm::abs(d.y));
                // if ( d.x < 0.5 && d.y < 0.5) {
                // } else {
                //     check_gate_pos(selected_gate);
                //     selected_gate->selected = 0;
                //     selected_gate = 0;
                // }
            }

            break;
        }

        case input_state::wiring:
        {
            temp_segment.p1 = snap_to_grid(mwloc);

            if (io.MouseClicked[0] && !imgui_hovered()) {
                add_wire(temp_segment.p1, temp_segment.p2);
                temp_segment.p2 = temp_segment.p1;
            }
            if (io.MouseClicked[1] && !imgui_hovered()) {
                istate = input_state::idle;
            }
            break;
        }

        case input_state::select: {
            if (io.MouseReleased[0]) {
                glm::vec2 p1 = mouse_press_loc_world;
                glm::vec2 p2 = get_mouse_world_pos();
                box_select(p1, p2);
                if (selection.size() > 0) {
                    istate = input_state::drag_wires;
                } else istate = input_state::idle;
            }
            break;
        }

        case input_state::drag_wires: {
            if (io.MouseClicked[0] && !imgui_hovered()) {
                mouse_press_loc_world = get_mouse_world_pos();
            }

            if (io.MouseDown[0] && !imgui_hovered()) {
                for (auto p : selection) {
                    *p += glm::round(get_mouse_world_pos() - mouse_press_loc_world);
                }
                mouse_press_loc_world = glm::round(get_mouse_world_pos());
            }

            if (io.MouseReleased[0]) {

                reduce_wires();

                // std::vector<wire2*> ws = wires;
                // selection.clear();
                // wires.clear();

                // for (auto w : ws) {
                //     for (auto& s: w->segments) {
                //         add_segment(s);
                //     }
                // }

                // for (auto w : ws) {
                //     delete w;
                // }

                istate = input_state::idle;
            }

            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_C)) {
                // copied_segments.clear();
                // if (selected_segments.size() > 0) {
                //     for (auto s : selected_segments) {
                //         copied_segments.push_back(*s);
                //     }
                // }
                istate = input_state::idle;
            }

            break;
        }
    }
}

void circuit::gui() {
    using namespace ImGui;

    BeginMainMenuBar();
    if (mode == mode::sim) {
        if (Button("sim")) {
            mode = mode::edit;
        }
    } else {
        if (Button("edit")) {
            mode = mode::sim;
        }
    }
    EndMainMenuBar();

    Begin("circuit");
    if (Button("add and_gate")) {
        gates.push_back(new and_gate);
    }

    if (Button("add or_gate")) {
        gates.push_back(new or_gate);
    }

    if (Button("add input_gate")) {
        gate* g = new input_gate;
        gates.push_back(g);
    }

    if (Button("add output_gate")) {
        gate* g = new output_gate;
        gates.push_back(g);
    }

    if (Button("add nand_gate")) {
        gates.push_back(new nand_gate);
    }

    // int total_seg = 0;
    // int total_joint = 0;
    // for (wire2* w : wires) {
    //     total_seg += w->segments.size();
    //     total_joint += w->points.size();
    // }

    Text("wires = %zu", wires.size());
    // Text("gates = %zu", gates.size());
    // Text("segments = %u", total_seg);
    // Text("joints = %u", total_joint);

    Text("selection = %lu", selection.size());
    // Text("copy = %lu", copied_segments.size());

    Text("input state = %s", input_state_str[istate]);

    Separator();

    if (Button("step")) {
        step();
    }

    if (Button("single step")) {
        single_step();
    }

    // for (int i = 0; i < ins.size(); i++) {
    //     PushID(i);
    //     if(Button(string_format("toggle in%d", i).c_str())) {
    //         input_gate* g = dynamic_cast<input_gate*>(ins[i]);
    //         if (g) {
    //             g->val = ~g->val;
    //             g->need_update = 1;
    //             step();
    //         }
    //     }
    //     PopID();
    // }

    // int t = 0;
    // for (gate* i : gates) {
    //     if (i->need_update) t++;
    // }

    // Text("%d gates need update", t);

    End();
}

// pin* circuit::select_pin(glm::vec2 loc) {

//     for ( gate* i : gates) {
//         for ( pin& p : i->pins) {
//             glm::vec2 d = loc - glm::vec2(i->pos + p.pos);
//             if (glm::length(d) < 0.2) {
//                 return &p;
//             }
//         }
//     }
//     return 0;
// }

// void circuit::select_gate(glm::vec2 loc){

//     bool b = false;

//     for (auto i = gates.begin(); i != gates.end(); ++i) {
//         glm::vec2 d = loc - glm::vec2((*i)->pos);
//         glm::vec2 box = (*i)->box;
//         if(d.x < box.x && d.x > -box.x && d.y < box.y && d.y > -box.y) {
//             b = true;
//             if(selected_gate && selected_gate != (*i)) selected_gate->selected = false;
//             selected_gate = (*i);
//             (*i)->selected = true;
//             select_diff = loc - glm::vec2((*i)->pos);
//             iter_swap(i, gates.end() - 1);
//             break;
//         };
//     }

//     if(!b) {
//         if(selected_gate) {
//             selected_gate->selected = false;
//             selected_gate = 0;
//         }
//     }
// }



// void circuit::delete_gate(gate *g) {

//     for (pin& p : g->pins) {
//         remove_connected_wire_form_pin(&p);
//     }

//     delete g;
//     auto it = std::find(gates.begin(),gates.end(), g);
//     if (it != gates.end()) {
//         gates.erase(it);
//     }

//     it = std::find(ins.begin(),ins.end(), g);
//     if (it != ins.end()) {
//         ins.erase(it);
//     }

//     it = std::find(outs.begin(),outs.end(), g);
//     if (it != outs.end()) {
//         outs.erase(it);
//     }
// }

// void circuit::delete_wire(wire2 *w) {
//     for (auto& p : w->pins) {
//         p->w = &dummy_wire;
//     }

//     delete w;

//     auto it = std::find(wires.begin(), wires.end(), w);
//     if (it != wires.end()) {
//         wires.erase(it);
//     }

// }

// void circuit::remove_connected_wire_form_pin(pin* p) {
//     if (p->w != &dummy_wire) {
//         auto it = std::find(p->w->pins.begin(), p->w->pins.end(), p);
//         if (it != p->w->pins.end()) {
//             p->w->pins.erase(it);
//         }
//         p->w = &dummy_wire;
//     }
// }

// void circuit::connect_wire_to_pin(pin* p , wire2* w) {

//     remove_connected_wire_form_pin(p);

//     p->w = w;
//     w->pins.push_back(p);
// }

// void circuit::check_gate_pos(gate* g) {

//     for (pin& p : g->pins) {
//         for (wire2* w : wires) {
//             for (auto& s : w->segments) {
//                 glm::ivec2 d1 = s.a - (g->pos + p.pos);
//                 glm::ivec2 d2 = s.b - (g->pos + p.pos);
//                 d1 = {glm::abs(d1.x), glm::abs(d1.y)};
//                 d2 = {glm::abs(d2.x), glm::abs(d2.y)};
//                 if ((d1.x < 0.5 && d1.y < 0.5) || (d2.x < 0.5 && d2.y < 0.5)) {
//                     connect_wire_to_pin(&p, w);
//                     goto f;
//                 }
//             }
//         }
//         remove_connected_wire_form_pin(&p);
//         f:;
//     }
// }

// void circuit::add_segment(wire2::segment& s) {
//     if (s.a == s.b) return;

//     for (auto w : wires) {
//         if (w->collides_with(s)) {
//             w->add_segment(s);
//             return;
//         }
//     }

//     wire2* w = new wire2;
//     w->add_segment(s);
//     wires.push_back(w);
// }

void circuit::box_select(glm::vec2 p1, glm::vec2 p2) {

    float xmin = p1.x < p2.x ? p1.x : p2.x;
    float xmax = p1.x > p2.x ? p1.x : p2.x;
    float ymin = p1.y < p2.y ? p1.y : p2.y;
    float ymax = p1.y > p2.y ? p1.y : p2.y;

    auto box = [&](glm::ivec2 p) {
        return xmin < p.x && p.x < xmax && ymin < p.y && p.y < ymax;
    };

    selection.clear();
    for (auto& s : wires) {
        if (box(s.p1)) {
            selection.insert(&s.p1);
        }

        if (box(s.p2)) {
            selection.insert(&s.p2);
        }
    }

    for (auto& g : gates) {
        if (box(g->pos)) {
            selection.insert(&g->pos);
        }
    }

}

void circuit::create_nets() {
    for (auto& w : wires) {
        for (auto& n : nets) {
        }
    }
}
