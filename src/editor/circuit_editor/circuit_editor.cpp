#include "circuit_editor.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

namespace logic::editor::circuit_editor {

circuit_editor::circuit_editor(std::string library_name, std::string name, std::unordered_map<std::string, library::library>& libs)
    : temp_wire(&temp_wire_p1, &temp_wire_p2)
    , libraries(libs)
{
    this->name = name;
    this->library_name = library_name;

    load_circuit();
}

void circuit_editor::draw(Hd::FrameBuffer* fb, int sx, int sy)
{
    if (fb == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, sx, sy);
    } else {
        fb->Bind(sx, sy);
    }

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    fbsize = {sx, sy};
    recalc_vp();

    if (_draw_grid) {
        draw_grid();
    }

    if (istate == input_state::add_wire2) {
        temp_wire.draw(vp, *g);
    }

    if (istate == input_state::add_wire1 || istate == input_state::add_wire2) {
        glm::vec2 mp = glm::round(screen_to_world(mpos) / 10.0f) * 10.0f;
        g->setColor({0.5, 0.6, 0.1, 1.0});
        g->line(vp, {mp.x, -1000}, {mp.x, 1000});
        g->line(vp, {-1000, mp.y}, {1000, mp.y});
    }

    if (istate == input_state::select2) {
        g->setColor({0.5, 0.6, 0.1, 1.0});
        g->rect(vp, select1_pos, select2_pos, false);
    }

    for (auto& w : wires) {
        w.second->draw(vp, *g);
    }

    for (auto& i : items) {
        i.second->draw(vp, *g);
    }

    if (temp_port.get()) {
        temp_port->draw(vp, *g);
    }

    if (temp_component.get()) {
        temp_component->draw(vp, *g);
    }

    for (auto& i : selected_items) {
        g->setColor({1, 1, 0, 1});
        g->rect(vp, glm::vec2{i->get_position()} - (i->get_box() + glm::vec2{1, 1}), glm::vec2{i->get_position()} + (i->get_box() + glm::vec2{1, 1}));
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void circuit_editor::process_inputs()
{

    editor::process_inputs();

    using namespace ImGui;

    switch (istate) {
    case input_state::idle: {

        if (IsKeyPressed(ImGuiKey_I)) {
            temp_port = std::make_unique<port>(port::port_type::input, 1, "in1");
            temp_port->set_position(screen_to_world(mpos));
            istate = input_state::add_port;
            break;
        }

        if (IsKeyPressed(ImGuiKey_O)) {
            temp_port = std::make_unique<port>(port::port_type::output, 1, "out1");
            temp_port->set_position(screen_to_world(mpos));
            istate = input_state::add_port;
            break;
        }

        if (IsKeyPressed(ImGuiKey_W)) {
            istate = input_state::add_wire1;
            break;
        }

        if (IsKeyPressed(ImGuiKey_S)) {
            istate = input_state::select1;
            break;
        }

        if (IsKeyPressed(ImGuiKey_G)) {
            _draw_grid = !_draw_grid;
        }

        // if (IsMouseClicked(ImGuiMouseButton_Left)) {
        //     selected_component = 0;
        //     auto p = screen_to_world(mpos);
        //     for (auto& c : components) {
        //         if (c->try_select(p)) {
        //             selected_component = c;
        //             break;
        //         }
        //     }
        // }

        if (IsMouseClicked(ImGuiMouseButton_Left)) {
            for (auto& i : items) {
                if (i.second->try_select(screen_to_world(mpos))) {
                    selected_items.clear();
                    selected_items.push_back(i.first);
                    move_start = screen_to_world(mpos);
                    istate = input_state::move1;
                    break;
                }
            }
        }

    } break;

    case input_state::add_component: {

        temp_component->set_position(screen_to_world(mpos));

        if (IsKeyPressed(ImGuiKey_Escape)) {
            temp_component.reset();
            istate = input_state::idle;
            break;
        }

        if (IsMouseClicked(ImGuiMouseButton_Left)) {
            components.insert(temp_component.get());

            items.insert({temp_component.get(), std::move(temp_component)});
            istate = input_state::idle;
            break;
        }

    } break;

    case input_state::add_port: {
        temp_port->set_position(screen_to_world(mpos));

        if (IsKeyPressed(ImGuiKey_Escape)) {
            temp_port.reset();
            istate = input_state::idle;
            break;
        }

        if (IsMouseClicked(ImGuiMouseButton_Left)) {
            ports.insert(temp_port.get());

            items.insert({temp_port.get(), std::move(temp_port)});
            istate = input_state::idle;
            break;
        }

    } break;

    case input_state::add_wire1: {

        if (IsKeyPressed(ImGuiKey_Escape)) {
            istate = input_state::idle;
            break;
        }

        if (IsMouseClicked(ImGuiMouseButton_Left)) {
            temp_wire.get_p1()->set_position(screen_to_world(mpos));
            temp_wire.get_p2()->set_position(screen_to_world(mpos));
            istate = input_state::add_wire2;
            break;
        }
    } break;

    case input_state::add_wire2: {
        temp_wire.get_p2()->set_position(screen_to_world(mpos));

        if (IsKeyPressed(ImGuiKey_Escape)) {
            reduce_wires();
            istate = input_state::idle;
            break;
        }

        if (IsMouseClicked(ImGuiMouseButton_Left)) {
            auto p1 = std::make_unique<wire_point>();
            auto p2 = std::make_unique<wire_point>();
            *p1 = *temp_wire.get_p1();
            *p2 = *temp_wire.get_p2();
            auto w = std::make_unique<wire>(p1.get(), p2.get());
            items.insert({p1.get(), std::move(p1)});
            items.insert({p2.get(), std::move(p2)});
            wires.insert({w.get(), std::move(w)});
            temp_wire.get_p1()->set_position(temp_wire.get_p2()->get_position());
            break;
        }
    } break;

    case input_state::select1: {
        if (IsKeyPressed(ImGuiKey_Escape)) {
            istate = input_state::idle;
            break;
        }
        if (IsMouseDown(ImGuiMouseButton_Left)) {
            istate = input_state::select2;
            select1_pos = screen_to_world(mpos);
            select2_pos = screen_to_world(mpos);
        }
    } break;

    case input_state::select2: {
        select2_pos = screen_to_world(mpos);
        if (!IsMouseDown(ImGuiMouseButton_Left)) {
            selected_items.clear();
            for (auto& i : items) {
                if (i.first->is_inside_rect(select1_pos, select2_pos)) {
                    selected_items.push_back(&*i.second);
                }
            }
            if (selected_items.size() != 0) {
                istate = input_state::move0;
            } else {
                istate = input_state::idle;
            }
        }
    } break;

    case input_state::move0: {
        if (IsKeyPressed(ImGuiKey_Escape)) {
            selected_items.clear();
            istate = input_state::idle;
        } else if (IsKeyPressed(ImGuiKey_Delete)) {

            for (auto& i : selected_items) {
                items.erase(i);
            }

            selected_items.clear();

            for (auto i = wires.begin(); i != wires.end(); i++) {
                if (items.find(i->first->get_p1()) == items.end() || items.find(i->first->get_p2()) == items.end()) {
                    items.erase(i->first->get_p1());
                    items.erase(i->first->get_p2());
                    wires.erase(i);
                }
            }

            for (auto i = components.begin(); i != components.end(); i++) {
                if (items.find(*i) == items.end()) {
                    components.erase(*i);
                }
            }

            for (auto i = ports.begin(); i != ports.end(); i++) {
                if (items.find(*i) == items.end()) {
                    ports.erase(*i);
                }
            }

            reduce_wires();
            istate = input_state::idle;

        } else if (IsMouseDown(ImGuiMouseButton_Left)) {
            move_start = screen_to_world(mpos);
            istate = input_state::move1;
        }
    } break;

    case input_state::move1: {

        move_end = screen_to_world(mpos);

        float move_dist = 10.0f;

        if (glm::abs(move_end.x - move_start.x) > move_dist / 2.0f) {
            int d = glm::round((move_end.x - move_start.x) / move_dist) * move_dist;
            for (auto& i : selected_items) {
                i->move({d, 0});
            }
            move_start.x += d;
        }

        if (glm::abs(move_end.y - move_start.y) > move_dist / 2.0f) {
            int d = glm::round((move_end.y - move_start.y) / move_dist) * move_dist;
            for (auto& i : selected_items) {
                i->move({0, d});
            }
            move_start.y += d;
        }

        if (!IsMouseDown(ImGuiMouseButton_Left)) {
            selected_items.clear();
            reduce_wires();
            istate = input_state::idle;
        }
    } break;
    }
}

void circuit_editor::reduce_wires()
{
    std::unordered_set<glm::ivec2> hori_points;
    std::unordered_set<glm::ivec2> vert_points;
    std::unordered_set<glm::ivec2> diag_points;

    std::vector<std::pair<glm::ivec2, glm::ivec2>> new_wires;

    struct swire {
        swire(int c, int p1, int p2)
        {
            this->c = c;
            if (p2 < p1) {
                this->p1 = p2;
                this->p2 = p1;
            } else {
                this->p1 = p1;
                this->p2 = p2;
            }
        }

        bool try_merge(const swire& other)
        {
            if (p2 < other.p1 || p1 > other.p2) {
                return false;
            } else {
                p1 = std::min(p1, other.p1);
                p2 = std::max(p2, other.p2);
                return true;
            }
        }

        bool inside(int p)
        {
            return p > p1 && p < p2;
        }

        int c;
        int p1;
        int p2;
    };

    struct swire_container {

        swire_container(int c)
        {
            this->c = c;
        }

        void add(swire w)
        {

            wires.push_back(w);

        loop:
            for (int i = 0; i < wires.size() - 1; i++) {
                if (wires[i].try_merge(wires.back())) {
                    std::iter_swap(wires.begin() + i, wires.end() - 2);
                    wires.pop_back();
                    goto loop;
                }
            }
        }

        void protect(int p)
        {
            for (int i = 0; i < wires.size(); i++) {
                swire& sw = wires[i];
                if (sw.inside(p)) {
                    int p2 = sw.p2;
                    sw.p2 = p;
                    wires.emplace_back(c, p, p2);
                    return;
                }
            }
        }

        int c;
        std::vector<swire> wires;
    };

    std::unordered_map<int, swire_container> hori;
    std::unordered_map<int, swire_container> vert;

    for (auto& w_ : wires) {
        auto w = std::pair<glm::ivec2, glm::ivec2>(w_.first->get_p1()->get_position(), w_.first->get_p2()->get_position());
        if (w.first == w.second) {
            continue;
        } else {
            auto p1 = w.first;
            auto p2 = w.second;
            if (p1.y == p2.y) { // horizontal

                swire sw(p1.y, p1.x, p2.x);

                auto c = hori.find(p1.y);

                if (c == hori.end()) {
                    auto r = hori.emplace(p1.y, swire_container{p1.y});
                    c = r.first;
                }

                c->second.add(sw);

                hori_points.insert(p1);
                hori_points.insert(p2);
            } else if (p1.x == p2.x) { // vertical

                swire sw(p1.x, p1.y, p2.y);

                auto c = vert.find(p1.x);

                if (c == vert.end()) {
                    auto r = vert.emplace(p1.x, swire_container{p1.x});
                    c = r.first;
                }

                c->second.add(sw);

                vert_points.insert(p1);
                vert_points.insert(p2);

            } else {
                if (std::find(new_wires.begin(), new_wires.end(), w) == new_wires.end()) {
                    new_wires.push_back(w);
                    diag_points.insert(p1);
                    diag_points.insert(p2);
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
        for (auto& sw : it.second.wires) {
            new_wires.emplace_back(glm::ivec2{sw.p1, sw.c}, glm::ivec2{sw.p2, sw.c});
        }
    }

    for (auto it : vert) {
        for (auto& sw : it.second.wires) {
            new_wires.emplace_back(glm::ivec2{sw.c, sw.p1}, glm::ivec2{sw.c, sw.p2});
        }
    }

    // printf("----------------\n");
    // printf("diag_points.size = %zu\n", diag_points.size());
    // printf("vert_points.size = %zu\n", vert_points.size());
    // printf("hori_points.size = %zu\n", hori_points.size());
    // printf("new_wires.size = %zu\n", new_wires.size());

    selected_items.clear();

    for (auto& w : wires) {
        items.erase(w.first->get_p1());
        items.erase(w.first->get_p2());
    }

    wires.clear();

    for (auto& w : new_wires) {
        auto wp1 = std::make_unique<wire_point>();
        auto wp2 = std::make_unique<wire_point>();
        auto w_ = std::make_unique<wire>(wp1.get(), wp2.get());

        wp1->set_position(w.first);
        wp2->set_position(w.second);

        items.insert({wp1.get(), std::move(wp1)});
        items.insert({wp2.get(), std::move(wp2)});

        wires.insert({w_.get(), std::move(w_)});
    }
}

void circuit_editor::property_editor()
{
    using namespace ImGui;
    Text("Item_Count = %zu", items.size());
    Text("wire_Count = %zu", wires.size());

    Separator();

    if (selected_items.size() == 1) {
        selected_items[0]->property_editor();
    }
}

std::vector<library::wire> circuit_editor::get_wires()
{
    std::vector<library::wire> r;

    for (auto& w : wires) {
        r.push_back({w.second->get_p1()->get_position(), w.second->get_p2()->get_position()});
    }

    return r;
}

std::vector<library::component> circuit_editor::get_components()
{
    std::vector<library::component> r;

    for (auto& c : components) {
        r.push_back({c->get_position(), c->library_name, c->name});
    }

    return r;
}

std::vector<library::port> circuit_editor::get_ports()
{
    std::vector<library::port> r;

    for (auto& p : ports) {
        library::port _p;

        _p.name = p->get_name();
        _p.number = p->get_number();

        switch (p->get_type()) {
        case port::port_type::input: _p.type = library::port::port_type::input; break;
        case port::port_type::output: _p.type = library::port::port_type::output; break;
        case port::port_type::inout: _p.type = library::port::port_type::inout; break;
        }

        r.push_back(std::move(_p));
    }

    return r;
}

void circuit_editor::load_circuit()
{
    auto l = libraries.find(library_name);

    if (l == libraries.end()) {
        printf("library %s does not exist\n", library_name.c_str());
        return;
    }

    auto c = l->second.circuits.find(name);

    if (c == l->second.circuits.end()) {
        printf("circuit %s does not exist in library %s\n", name.c_str(), library_name.c_str());
        return;
    }

    for (auto& w : c->second.wires) {
        auto p1 = std::make_unique<wire_point>();
        auto p2 = std::make_unique<wire_point>();

        p1->set_position(w.p1);
        p2->set_position(w.p2);

        auto _w = std::make_unique<wire>(p1.get(), p2.get());

        items.insert({p1.get(), std::move(p1)});
        items.insert({p2.get(), std::move(p2)});

        wires.insert({_w.get(), std::move(_w)});
    }

    for (auto& co : c->second.components) {

        std::string fname = co.library_name + "::" + co.name;
        symbol::symbol* s;

        auto it = symbols.find(fname);
        if (it != symbols.end()) {
            s = it->second.get();

        } else {
            s = load_symbol(co.library_name, co.name);
        }

        if (!s) {
            continue;
        }

        auto _co = std::make_unique<component>();
        _co->set_box(s->box);
        _co->set_position(co.position);
        _co->library_name = co.library_name;
        _co->name = co.name;
        _co->symbol = s;

        components.insert(_co.get());
        items.insert({_co.get(), std::move(_co)});
    }

    for (auto& p : c->second.ports) {
        port::port_type t;
        switch (p.type) {
        case library::port::port_type::input: t = port::port_type::input; break;
        case library::port::port_type::output: t = port::port_type::output; break;
        case library::port::port_type::inout: t = port::port_type::inout; break;
        }
        auto _p = std::make_unique<port>(t, p.number, p.name);
        _p->set_position(p.position);

        ports.insert(_p.get());
        items.insert({_p.get(), std::move(_p)});
    }
}

symbol::symbol* circuit_editor::load_symbol(std::string library_name, std::string name)
{
    auto l = libraries.find(library_name);
    if (l != libraries.end()) {
        auto c = l->second.circuits.find(name);
        if (c != l->second.circuits.end()) {
            auto s = std::make_unique<symbol::symbol>();
            *s = c->second.symbol;
            auto& _s = symbols[library_name + "::" + name];
            _s = std::move(s);
            return _s.get();
        }
    }
    printf("load_symbol failed\n");
    return 0;
}

void circuit_editor::add_component(std::string library_name, std::string name)
{
    if (istate != input_state::idle) {
        return;
    }

    auto s = load_symbol(library_name, name);

    if (!s) {
        return;
    }

    temp_component = std::make_unique<component>();
    temp_component->library_name = library_name;
    temp_component->name = name;
    temp_component->set_box(s->box);
    temp_component->set_position(screen_to_world(mpos));
    temp_component->symbol = s;

    istate = input_state::add_component;
}

}