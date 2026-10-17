#include "symbol_editor.h"

namespace logic::editor::symbol_editor {

symbol_editor::symbol_editor(std::string library_name, std::string name, std::unordered_map<std::string, library::library>& libs)
    : temp_line(&cp1, &cp2)
    , temp_bezier(&cp1, &cp2, &cp3)
    , temp_circle(&cp1, &cp2)
    , libraries(libs)
{
    this->name = name;
    this->library_name = library_name;

    load_symbol();
}

void symbol_editor::draw(Hd::FrameBuffer* fb, int sx, int sy)
{
    if (fb == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, sx, sy);
    } else {
        fb->Bind(sx, sy);
    }

    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    fbsize = {sx, sy};
    recalc_vp();

    draw_grid();

    // draw center
    g->setColor({1, 1, 1, 1});
    g->line(vp, {-5, 0}, {5, 0});
    g->line(vp, {0, -5}, {0, 5});
    g->circle(vp, {0, 0}, 2.5);

    if (istate == input_state::add_pin) {
        temp_pin.draw(vp, *g);
    }

    if (istate == input_state::add_line1) {
        cp1.draw(vp, *g);
    }

    if (istate == input_state::add_line2) {
        cp1.draw(vp, *g);
        cp2.draw(vp, *g);
        temp_line.draw(vp, *g);
    }

    if (istate == input_state::add_bezier1) {
        cp1.draw(vp, *g);
    }

    if (istate == input_state::add_bezier2) {
        cp1.draw(vp, *g);
        cp2.draw(vp, *g);
    }

    if (istate == input_state::add_bezier3) {
        cp1.draw(vp, *g);
        cp2.draw(vp, *g);
        cp3.draw(vp, *g);
        temp_bezier.draw(vp, *g);
    }

    if (istate == input_state::add_text) {
        temp_text.draw(vp, *g);
    }

    if (istate == input_state::add_circle1) {
        cp1.draw(vp, *g);
    }

    if (istate == input_state::add_circle2) {
        cp1.draw(vp, *g);
        cp2.draw(vp, *g);
        temp_circle.draw(vp, *g);
    }

    if (istate == input_state::select2) {
        g->setColor({0.5, 0.6, 0.1, 1.0});
        g->rect(vp, select1_pos, select2_pos, false);
    }

    for (auto& i : items) {
        i.second->draw(vp, *g);
    }

    for (auto& i : lines) {
        i->draw(vp, *g);
    }

    for (auto& i : beziers) {
        i->draw(vp, *g);
    }

    for (auto& i : texts) {
        i->draw(vp, *g);
    }

    for (auto& i : circles) {
        i->draw(vp, *g);
    }

    for (auto& i : pins) {
        i->draw(vp, *g);
    }

    for (auto& i : selected_items) {
        g->setColor({1, 1, 0, 1});
        g->rect(vp, glm::vec2{i->get_position()} - i->get_box(), glm::vec2{i->get_position()} + i->get_box());
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void symbol_editor::process_inputs()
{
    editor::process_inputs();

    using namespace ImGui;

    switch (istate) {
    case input_state::idle: {
        if (IsKeyPressed(ImGuiKey_P)) {
            temp_pin.set_position(screen_to_world(mpos));
            istate = input_state::add_pin;
            break;
        }
        if (IsKeyPressed(ImGuiKey_L)) {
            istate = input_state::add_line1;
            cp1.set_snap(false);
            cp1.set_position(screen_to_world(mpos));
            break;
        }
        if (IsKeyPressed(ImGuiKey_B)) {
            istate = input_state::add_bezier1;
            cp1.set_snap(false);
            cp1.set_position(screen_to_world(mpos));
            break;
        }
        if (IsKeyPressed(ImGuiKey_T)) {
            temp_text.set_position(screen_to_world(mpos));
            istate = input_state::add_text;
            break;
        }
        if (IsKeyPressed(ImGuiKey_C)) {
            istate = input_state::add_circle1;
            cp1.set_snap(false);
            cp1.set_position(screen_to_world(mpos));
            break;
        }
        if (IsKeyPressed(ImGuiKey_S)) {
            istate = input_state::select1;
            break;
        }
        if (IsMouseClicked(ImGuiMouseButton_Left)) {
            for (auto& i : items) {
                if (i.second->try_select(screen_to_world(mpos))) {
                    selected_items.clear();
                    selected_items.push_back(i.first);
                    move_snap = i.second->get_snap();
                    move_start = screen_to_world(mpos);
                    istate = input_state::move1;
                    break;
                }
            }
        }
    } break;

    case input_state::add_pin: {

        temp_pin.set_position(screen_to_world(mpos));

        if (IsKeyPressed(ImGuiKey_Escape)) {
            istate = input_state::idle;
            break;
        }

        if (IsMouseClicked(ImGuiMouseButton_Left)) {

            auto p = std::make_unique<pin>();
            *p = temp_pin;

            pins.insert(p.get());
            items.insert({p.get(), std::move(p)});

            istate = input_state::idle;
            break;
        }

    } break;

    case input_state::add_line1: {
        cp1.set_position(screen_to_world(mpos));

        if (IsKeyPressed(ImGuiKey_Escape)) {
            istate = input_state::idle;
            break;
        }

        if (IsMouseClicked(ImGuiMouseButton_Left)) {
            cp2 = cp1;
            istate = input_state::add_line2;
            break;
        }

    } break;

    case input_state::add_line2: {
        cp2.set_position(screen_to_world(mpos));

        if (IsKeyPressed(ImGuiKey_Escape)) {
            istate = input_state::idle;
            break;
        }

        if (IsMouseClicked(ImGuiMouseButton_Left)) {
            auto _cp1 = std::make_unique<control_point>();
            *_cp1 = cp1;

            auto _cp2 = std::make_unique<control_point>();
            *_cp2 = cp2;

            auto l = std::make_unique<line>(_cp1.get(), _cp2.get());

            items.insert({_cp1.get(), std::move(_cp1)});
            items.insert({_cp2.get(), std::move(_cp2)});
            lines.insert(std::move(l));

            istate = input_state::idle;
            break;
        }
    } break;

    case input_state::add_bezier1: {
        cp1.set_position(screen_to_world(mpos));

        if (IsKeyPressed(ImGuiKey_Escape)) {
            istate = input_state::idle;
            break;
        }

        if (IsMouseClicked(ImGuiMouseButton_Left)) {
            cp2 = cp1;
            istate = input_state::add_bezier2;
            break;
        }
    } break;

    case input_state::add_bezier2: {
        cp2.set_position(screen_to_world(mpos));

        if (IsKeyPressed(ImGuiKey_Escape)) {
            istate = input_state::idle;
            break;
        }

        if (IsMouseClicked(ImGuiMouseButton_Left)) {
            cp3 = cp2;
            istate = input_state::add_bezier3;
            break;
        }
    } break;

    case input_state::add_bezier3: {
        cp3.set_position(screen_to_world(mpos));

        if (IsKeyPressed(ImGuiKey_Escape)) {
            istate = input_state::idle;
            break;
        }

        if (IsMouseClicked(ImGuiMouseButton_Left)) {
            auto _cp1 = std::make_unique<control_point>();
            *_cp1 = cp1;

            auto _cp2 = std::make_unique<control_point>();
            *_cp2 = cp2;

            auto _cp3 = std::make_unique<control_point>();
            *_cp3 = cp3;

            auto b = std::make_unique<bezier>(_cp1.get(), _cp2.get(), _cp3.get());

            items.insert({_cp1.get(), std::move(_cp1)});
            items.insert({_cp2.get(), std::move(_cp2)});
            items.insert({_cp3.get(), std::move(_cp3)});
            beziers.insert(std::move(b));

            istate = input_state::idle;
            break;
        }
    } break;

    case input_state::add_text: {
        temp_text.set_position(screen_to_world(mpos));

        if (IsKeyPressed(ImGuiKey_Escape)) {
            istate = input_state::idle;
            break;
        }

        if (IsMouseClicked(ImGuiMouseButton_Left)) {
            auto t = std::make_unique<text>();
            *t = temp_text;

            texts.insert(t.get());
            items.insert({t.get(), std::move(t)});

            istate = input_state::idle;
            break;
        }
    } break;

    case input_state::add_circle1: {
        cp1.set_position(screen_to_world(mpos));

        if (IsKeyPressed(ImGuiKey_Escape)) {
            istate = input_state::idle;
            break;
        }

        if (IsMouseClicked(ImGuiMouseButton_Left)) {
            cp2 = cp1;
            istate = input_state::add_circle2;
            break;
        }
    } break;

    case input_state::add_circle2: {
        cp2.set_position(screen_to_world(mpos));

        if (IsKeyPressed(ImGuiKey_Escape)) {
            istate = input_state::idle;
            break;
        }

        if (IsMouseClicked(ImGuiMouseButton_Left)) {
            auto _cp1 = std::make_unique<control_point>();
            *_cp1 = cp1;
            auto _cp2 = std::make_unique<control_point>();
            *_cp2 = cp2;

            auto b = std::make_unique<circle>(_cp1.get(), _cp2.get());

            items.insert({_cp1.get(), std::move(_cp1)});
            items.insert({_cp2.get(), std::move(_cp2)});
            circles.insert(std::move(b));

            istate = input_state::idle;
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
            move_snap = false;
            for (auto& i : items) {
                if (i.first->is_inside_rect(select1_pos, select2_pos)) {
                    selected_items.push_back(&*i.second);
                    if (i.second->get_snap()) move_snap = true;
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

            for (auto i = lines.begin(); i != lines.end(); i++) {
                if (items.find(i->get()->p1) == items.end() || items.find(i->get()->p2) == items.end()) {
                    items.erase(i->get()->p1);
                    items.erase(i->get()->p2);
                    lines.erase(i);
                }
            }

            for (auto i = beziers.begin(); i != beziers.end(); i++) {
                if (items.find(i->get()->p1) == items.end() || items.find(i->get()->p2) == items.end() || items.find(i->get()->p3) == items.end()) {
                    items.erase(i->get()->p1);
                    items.erase(i->get()->p2);
                    items.erase(i->get()->p3);
                    beziers.erase(i);
                }
            }

            for (auto i = circles.begin(); i != circles.end(); i++) {
                if (items.find(i->get()->p1) == items.end() || items.find(i->get()->p2) == items.end()) {
                    items.erase(i->get()->p1);
                    items.erase(i->get()->p2);
                    circles.erase(i);
                }
            }

            for (auto i = texts.begin(); i != texts.end(); i++) {
                if (items.find(*i) == items.end()) {
                    items.erase(*i);
                    texts.erase(i);
                }
            }

            for (auto i = pins.begin(); i != pins.end(); i++) {
                if (items.find(*i) == items.end()) {
                    items.erase(*i);
                    pins.erase(i);
                }
            }

        } else if (IsMouseDown(ImGuiMouseButton_Left)) {
            move_start = screen_to_world(mpos);
            istate = input_state::move1;
        }
    } break;

    case input_state::move1: {

        move_end = screen_to_world(mpos);

        move_dist = move_snap ? 10.0f : 1.0f;

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
            istate = input_state::idle;
            selected_items.clear();
        }
    } break;
    }
}

void symbol_editor::property_editor()
{
    using namespace ImGui;

    Separator();

    if (selected_items.size() == 1) {
        selected_items[0]->property_editor();
    }
}

symbol::symbol symbol_editor::get_symbol()
{
    symbol::symbol r;

    for (auto& l : lines) {
        r.lines.push_back({l->p1->get_position(), l->p2->get_position()});
    }
    for (auto& b : beziers) {
        r.beziers.push_back({b->p1->get_position(), b->p2->get_position(), b->p3->get_position()});
    }
    for (auto& c : circles) {
        r.circles.push_back({c->p1->get_position(), c->p2->get_position()});
    }
    for (auto& t : texts) {
        r.texts.push_back({t->t, t->size, t->get_position()});
    }
    for (auto& p : pins) {
        r.pins.push_back({p->name, p->number, p->get_position()});
    }

    return r;
}

void symbol_editor::load_symbol()
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

    auto& s = c->second.symbol;

    for (auto& l : s.lines) {
        auto cp1 = std::make_unique<control_point>();
        auto cp2 = std::make_unique<control_point>();
        cp1->set_position(l.p1);
        cp2->set_position(l.p2);
        auto _l = std::make_unique<line>(cp1.get(), cp2.get());
        items.insert({cp1.get(), std::move(cp1)});
        items.insert({cp2.get(), std::move(cp2)});
        lines.insert(std::move(_l));
    }

    for (auto& b : s.beziers) {
        auto cp1 = std::make_unique<control_point>();
        auto cp2 = std::make_unique<control_point>();
        auto cp3 = std::make_unique<control_point>();
        cp1->set_position(b.p1);
        cp2->set_position(b.p2);
        cp3->set_position(b.p3);
        auto _b = std::make_unique<bezier>(cp1.get(), cp2.get(), cp3.get());
        items.insert({cp1.get(), std::move(cp1)});
        items.insert({cp2.get(), std::move(cp2)});
        items.insert({cp3.get(), std::move(cp3)});
        beziers.insert(std::move(_b));
    }

    for (auto& c : s.circles) {
        auto cp1 = std::make_unique<control_point>();
        auto cp2 = std::make_unique<control_point>();
        cp1->set_position(c.p1);
        cp2->set_position(c.p2);
        auto _c = std::make_unique<circle>(cp1.get(), cp2.get());
        items.insert({cp1.get(), std::move(cp1)});
        items.insert({cp2.get(), std::move(cp2)});
        circles.insert(std::move(_c));
    }

    for (auto& t : s.texts) {
        auto _t = std::make_unique<text>();
        _t->set_position(t.pos);
        _t->size = t.size;
        _t->t = t.t;
        texts.insert(_t.get());
        items.insert({_t.get(), std::move(_t)});
    }

    for (auto& p : s.pins) {
        auto _p = std::make_unique<pin>();
        _p->set_position(p.pos);
        _p->name = p.name;
        _p->number = p.number;
        pins.insert(_p.get());
        items.insert({_p.get(), std::move(_p)});
    }
}

}