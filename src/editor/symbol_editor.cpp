#include "symbol_editor.h"

#include <algorithm>

namespace logic::editor {

namespace {

float distance_to_segment(glm::vec2 p, glm::vec2 a, glm::vec2 b)
{
    auto ab = b - a;
    float l2 = glm::dot(ab, ab);
    if (l2 == 0.0f) return glm::length(p - a);
    float t = glm::clamp(glm::dot(p - a, ab) / l2, 0.0f, 1.0f);
    return glm::length(p - (a + t * ab));
}

float distance_to_shape(glm::vec2 p, const model::shape& s)
{
    if (auto l = std::get_if<model::line>(&s.geom)) {
        return distance_to_segment(p, l->a, l->b);
    } else if (auto b = std::get_if<model::bezier>(&s.geom)) {
        float d = 1e30f;
        glm::vec2 prev = b->a;
        for (int k = 1; k <= 16; k++) {
            float t = k / 16.0f;
            glm::vec2 q = (1 - t) * (1 - t) * glm::vec2(b->a) + 2 * (1 - t) * t * glm::vec2(b->c) + t * t * glm::vec2(b->b);
            d = std::min(d, distance_to_segment(p, prev, q));
            prev = q;
        }
        return d;
    } else if (auto c = std::get_if<model::circle>(&s.geom)) {
        return std::abs(glm::length(p - glm::vec2(c->center)) - c->radius);
    } else if (auto t = std::get_if<model::text>(&s.geom)) {
        // rough box from the anchor, the model has no font metrics
        glm::vec2 size{t->str.size() * t->size * 0.6f, t->size};
        auto rel = p - glm::vec2(t->pos);
        return (rel.x >= 0 && rel.y >= 0 && rel.x <= size.x && rel.y <= size.y) ? 0.0f : 1e30f;
    }
    return 1e30f;
}

glm::ivec2 round(glm::vec2 p) { return glm::ivec2(glm::round(p)); }

}

symbol_editor::symbol_editor(context c)
    : ctx(c)
    , sym(c.cell.symbol)
{
}

void symbol_editor::begin_shape(model::shape s)
{
    selection.clear();
    draft = std::move(s);
    draft_step = 0;
    mode = mode_t::add_shape;
}

void symbol_editor::process_inputs(bool hovered)
{
    using namespace ImGui;

    if (hovered) view.handle_view_input();

    auto m = view.mouse_world();
    bool shift = GetIO().KeyShift;

    if (hovered && IsKeyPressed(ImGuiKey_G)) view.show_grid = !view.show_grid;

    switch (mode) {
    case mode_t::idle: {
        if (!hovered) break;

        if (IsKeyPressed(ImGuiKey_P)) {
            draft_pin = {};
            // link to the first port that has no pin yet
            for (auto& p : ctx.cell.ports) {
                if (!sym.find_pin_of_port(p.id)) {
                    draft_pin.port = p.id;
                    break;
                }
            }
            draft_pin.pos = model::snap(m, model::grid);
            selection.clear();
            mode = mode_t::add_pin;
        } else if (IsKeyPressed(ImGuiKey_L)) {
            begin_shape({model::no_id, model::line{}});
        } else if (IsKeyPressed(ImGuiKey_B)) {
            begin_shape({model::no_id, model::bezier{}});
        } else if (IsKeyPressed(ImGuiKey_C)) {
            begin_shape({model::no_id, model::circle{{0, 0}, 0.0f}});
        } else if (IsKeyPressed(ImGuiKey_T)) {
            begin_shape({model::no_id, model::text{}});
        } else if (IsKeyPressed(ImGuiKey_Delete)) {
            delete_selection();
        } else if (IsKeyPressed(ImGuiKey_Escape)) {
            selection.clear();
        } else if (IsMouseClicked(ImGuiMouseButton_Left)) {
            auto hs = hit_test(m);
            auto bounds = selection_bounds();
            bool in_bounds = hs.empty() && !shift && bounds && bounds->contains(m);
            if (hs.empty() && !in_bounds) {
                if (!shift) selection.clear();
                box_start = m;
                mode = mode_t::box_select;
            } else {
                if (!hs.empty() && !is_selected(hs[0])) select(hs, shift);
                // pins live on the schematic grid, shapes on single units
                bool has_pin = std::any_of(selection.begin(), selection.end(), [](auto& h) { return h.kind == handle::pin; });
                drag_step = has_pin ? model::grid : 1;
                drag_start = m;
                drag_applied = {0, 0};
                drag_moved = false;
                drag_from_bounds = in_bounds;
                mode = mode_t::moving;
            }
        }
    } break;

    case mode_t::moving: {
        auto d = model::snap(m - drag_start, drag_step) - drag_applied;
        if (d != glm::ivec2{0, 0}) {
            move_selection(d);
            drag_applied += d;
            drag_moved = true;
        }
        if (!IsMouseDown(ImGuiMouseButton_Left)) {
            // a plain click on empty space still clears the selection
            if (!drag_moved && drag_from_bounds) selection.clear();
            mode = mode_t::idle;
        }
    } break;

    case mode_t::box_select: {
        if (!IsMouseDown(ImGuiMouseButton_Left)) {
            select(handles_in(model::make_rect(box_start, m)), true);
            mode = mode_t::idle;
        }
    } break;

    case mode_t::add_pin: {
        draft_pin.pos = model::snap(m, model::grid);
        if (!hovered) break;

        if (IsKeyPressed(ImGuiKey_Escape)) {
            mode = mode_t::idle;
        } else if (IsMouseClicked(ImGuiMouseButton_Left)) {
            draft_pin.id = ctx.cell.new_id();
            sym.pins.push_back(draft_pin);
            selection = {{handle::pin, draft_pin.id}};
            ctx.modified();
            mode = mode_t::idle;
        }
    } break;

    case mode_t::add_shape: {
        // points not placed yet follow the mouse
        for (int i = draft_step; i < model::point_count(draft); i++)
            model::set_point(draft, i, round(m));
        if (!hovered) break;

        if (IsKeyPressed(ImGuiKey_Escape)) {
            mode = mode_t::idle;
        } else if (IsMouseClicked(ImGuiMouseButton_Left)) {
            draft_step++;
            if (draft_step == model::point_count(draft)) {
                draft.id = ctx.cell.new_id();
                sym.shapes.push_back(draft);
                ctx.modified();
                mode = mode_t::idle;
            }
        }
    } break;
    }
}

std::vector<symbol_editor::handle> symbol_editor::hit_test(glm::vec2 p)
{
    float tol = view.pixels(6.0f);

    for (auto& pin : sym.pins) {
        auto d = glm::abs(p - glm::vec2(pin.pos));
        if (d.x <= std::max(2.5f, tol) && d.y <= std::max(2.5f, tol)) return {{handle::pin, pin.id}};
    }

    // all points at the same spot move together (e.g. corners of a box)
    std::vector<handle> r;
    for (auto& s : sym.shapes)
        for (int i = 0; i < model::point_count(s); i++)
            if (glm::length(p - glm::vec2(model::get_point(s, i))) <= tol) r.push_back({handle::point, s.id, i});
    if (!r.empty()) return r;

    for (auto it = sym.shapes.rbegin(); it != sym.shapes.rend(); ++it) {
        if (distance_to_shape(p, *it) <= tol) {
            for (int i = 0; i < model::point_count(*it); i++)
                r.push_back({handle::point, it->id, i});
            return r;
        }
    }

    return {};
}

std::vector<symbol_editor::handle> symbol_editor::handles_in(model::rect rc)
{
    std::vector<handle> r;
    for (auto& p : sym.pins)
        if (rc.contains(p.pos)) r.push_back({handle::pin, p.id});
    for (auto& s : sym.shapes)
        for (int i = 0; i < model::point_count(s); i++)
            if (rc.contains(model::get_point(s, i))) r.push_back({handle::point, s.id, i});
    return r;
}

std::optional<model::rect> symbol_editor::selection_bounds() const
{
    std::optional<model::rect> r;
    auto add = [&](glm::vec2 p) {
        if (r) r->expand(p);
        else r = model::rect{p, p};
    };

    for (auto& h : selection) {
        if (h.kind == handle::pin) {
            if (auto p = sym.find_pin(h.id)) add(p->pos);
        } else if (auto s = sym.find_shape(h.id)) {
            add(model::get_point(*s, h.index));
        }
    }

    if (r) {
        r->min -= 2.5f;
        r->max += 2.5f;
    }
    return r;
}

bool symbol_editor::is_selected(handle h) const
{
    return std::find(selection.begin(), selection.end(), h) != selection.end();
}

bool symbol_editor::is_shape_selected(model::id shape) const
{
    return std::any_of(selection.begin(), selection.end(), [&](auto& h) { return h.kind == handle::point && h.id == shape; });
}

void symbol_editor::select(const std::vector<handle>& hs, bool add)
{
    if (!add) selection.clear();
    for (auto& h : hs)
        if (!is_selected(h)) selection.push_back(h);
}

void symbol_editor::move_selection(glm::ivec2 d)
{
    for (auto& h : selection) {
        if (h.kind == handle::pin) {
            if (auto p = sym.find_pin(h.id)) p->pos += d;
            continue;
        }

        auto s = sym.find_shape(h.id);
        if (!s) continue;

        // a circle with its center selected moves as a whole, the radius
        // handle must not change the radius then
        bool is_circle = std::holds_alternative<model::circle>(s->geom);
        if (is_circle && h.index == 1 && is_selected({handle::point, h.id, 0})) continue;

        model::set_point(*s, h.index, model::get_point(*s, h.index) + d);
    }
    ctx.modified();
}

void symbol_editor::delete_selection()
{
    if (selection.empty()) return;

    for (auto& h : selection) {
        if (h.kind == handle::pin)
            std::erase_if(sym.pins, [&](auto& p) { return p.id == h.id; });
        else
            std::erase_if(sym.shapes, [&](auto& s) { return s.id == h.id; });
    }

    selection.clear();
    ctx.modified();
}

void symbol_editor::validate_selection()
{
    std::erase_if(selection, [&](const handle& h) {
        return h.kind == handle::pin ? sym.find_pin(h.id) == nullptr : sym.find_shape(h.id) == nullptr;
    });
}

void symbol_editor::draw()
{
    auto& r = ctx.r;
    auto& vp = view.vp();

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    if (view.show_grid) r.grid(vp, view.get_zoom());

    // symbols might be replaced from the properties panel
    validate_selection();

    // origin of the symbol, the instance position in schematics
    r.g.setColor(render::colors::preview);
    r.g.line(vp, {-5, 0}, {5, 0});
    r.g.line(vp, {0, -5}, {0, 5});
    r.g.circle(vp, {0, 0}, 2.5f);

    for (auto& s : sym.shapes) {
        auto color = is_shape_selected(s.id) ? render::colors::selection : render::colors::symbol;
        render::draw_shape(r, vp, s, {0, 0}, {}, color);

        for (int i = 0; i < model::point_count(s); i++) {
            bool sel = is_selected({handle::point, s.id, i});
            r.g.setColor(sel ? render::colors::selection : render::colors::control_point);
            r.g.circle(vp, model::get_point(s, i), sel ? 1.5f : 1.0f, true);
        }
    }

    auto draw_pin = [&](const model::pin& p, glm::vec4 color) {
        auto port = ctx.cell.find_port(p.port);
        glm::vec2 pos = p.pos;
        r.g.setColor(port ? color : render::colors::error);
        r.g.rect(vp, pos - 2.5f, pos + 2.5f);
        r.text(vp, port ? port->name : "?", pos + glm::vec2{0, 4}, 6.0f, port ? render::colors::pin : render::colors::error, render::align::center);
    };

    for (auto& p : sym.pins)
        draw_pin(p, is_selected({handle::pin, p.id}) ? render::colors::selection : render::colors::pin);

    switch (mode) {
    case mode_t::add_pin:
        draw_pin(draft_pin, render::colors::preview);
        break;
    case mode_t::add_shape:
        render::draw_shape(r, vp, draft, {0, 0}, {}, render::colors::preview);
        r.g.setColor(render::colors::control_point);
        for (int i = 0; i < model::point_count(draft); i++)
            r.g.circle(vp, model::get_point(draft, i), 1.0f, true);
        break;
    case mode_t::box_select:
        r.g.setColor(render::colors::guide);
        r.g.rect(vp, box_start, view.mouse_world());
        break;
    default:
        if (selection.size() > 1)
            if (auto b = selection_bounds()) {
                r.g.setColor(render::colors::guide);
                r.g.rect(vp, b->min, b->max);
            }
        break;
    }
}

void symbol_editor::property_panel()
{
    using namespace ImGui;

    if (selection.size() == 1 && selection[0].kind == handle::pin) {
        auto pin = sym.find_pin(selection[0].id);
        if (!pin) return;

        SeparatorText("Pin");
        auto port = ctx.cell.find_port(pin->port);
        if (BeginCombo("Port", port ? port->name.c_str() : "<none>")) {
            for (auto& p : ctx.cell.ports) {
                auto label = p.name + " (" + model::to_string(p.dir) + ")";
                if (Selectable(label.c_str(), p.id == pin->port)) {
                    pin->port = p.id;
                    ctx.modified();
                }
            }
            EndCombo();
        }
        return;
    }

    // all selected points belong to one shape
    if (!selection.empty() && std::all_of(selection.begin(), selection.end(), [&](auto& h) { return h.kind == handle::point && h.id == selection[0].id; })) {
        auto s = sym.find_shape(selection[0].id);
        if (!s) return;

        if (auto t = std::get_if<model::text>(&s->geom)) {
            SeparatorText("Text");
            if (input_string("Text", t->str)) ctx.modified();
            if (SliderFloat("Size", &t->size, 1.0f, 40.0f)) ctx.modified();
        } else if (auto c = std::get_if<model::circle>(&s->geom)) {
            SeparatorText("Circle");
            if (InputInt2("Center", &c->center.x)) ctx.modified();
            if (InputFloat("Radius", &c->radius, 0.5f, 5.0f)) {
                c->radius = std::max(c->radius, 0.0f);
                ctx.modified();
            }
        } else {
            SeparatorText(std::holds_alternative<model::line>(s->geom) ? "Line" : "Bezier");
            for (int i = 0; i < model::point_count(*s); i++) {
                auto p = model::get_point(*s, i);
                PushID(i);
                if (InputInt2(i == 2 ? "Control" : (i == 0 ? "Start" : "End"), &p.x)) {
                    model::set_point(*s, i, p);
                    ctx.modified();
                }
                PopID();
            }
        }
        return;
    }

    SeparatorText("Symbol");
    Text("%zu shapes, %zu pins", sym.shapes.size(), sym.pins.size());

    if (Button("Generate from ports")) {
        if (sym.shapes.empty() && sym.pins.empty()) {
            ctx.cell.generate_symbol();
            ctx.modified();
        } else {
            OpenPopup("Replace symbol?");
        }
    }
    SetItemTooltip("Replace the symbol with a box, inputs on the left and outputs on the right");

    if (BeginPopupModal("Replace symbol?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        TextUnformatted("This replaces all shapes and pins of the symbol.");
        if (Button("Replace")) {
            ctx.cell.generate_symbol();
            selection.clear();
            ctx.modified();
            CloseCurrentPopup();
        }
        SameLine();
        if (Button("Cancel")) CloseCurrentPopup();
        EndPopup();
    }

    for (auto& problem : ctx.cell.check())
        TextColored({1, 0.6f, 0.2f, 1}, "%s", problem.c_str());
}

}
