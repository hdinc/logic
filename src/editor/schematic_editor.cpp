#include "schematic_editor.h"

#include <algorithm>
#include <map>
#include <set>

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

struct ivec2_less {
    bool operator()(glm::ivec2 a, glm::ivec2 b) const { return a.x != b.x ? a.x < b.x : a.y < b.y; }
};

}

schematic_editor::schematic_editor(context c)
    : ctx(c)
    , sch(*c.cell.get_schematic())
{
}

void schematic_editor::begin_place(model::id cell)
{
    if (mode != mode_t::idle) return;
    if (ctx.ws.would_create_cycle(ctx.cell.id, cell)) return;

    placing = {};
    placing.cell = cell;
    placing.pos = model::snap(view.mouse_world(), model::grid);
    mode = mode_t::place_instance;
}

void schematic_editor::process_inputs(bool hovered)
{
    using namespace ImGui;

    if (hovered) view.handle_view_input();

    auto m = view.mouse_world();
    auto ms = model::snap(m, model::grid);
    bool shift = GetIO().KeyShift;

    if (hovered && IsKeyPressed(ImGuiKey_G)) view.show_grid = !view.show_grid;

    switch (mode) {
    case mode_t::idle: {
        if (!hovered) break;

        if (IsKeyPressed(ImGuiKey_W)) {
            selection.clear();
            wire_started = false;
            mode = mode_t::wire;
        } else if (IsKeyPressed(ImGuiKey_I) || IsKeyPressed(ImGuiKey_O)) {
            placing_port = {};
            placing_port.dir = IsKeyPressed(ImGuiKey_I) ? model::direction::input : model::direction::output;
            placing_port.name = ctx.cell.unique_port_name(placing_port.dir == model::direction::input ? "in" : "out");
            placing_port.pos = ms;
            mode = mode_t::place_port;
        } else if (IsKeyPressed(ImGuiKey_Delete)) {
            delete_selection();
        } else if (IsKeyPressed(ImGuiKey_Escape)) {
            selection.clear();
        } else if (IsKeyPressed(ImGuiKey_R)) {
            rotate_selection();
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
                drag_start = m;
                drag_applied = {0, 0};
                drag_moved = false;
                drag_from_bounds = in_bounds;
                mode = mode_t::moving;
            }
        }
    } break;

    case mode_t::moving: {
        auto d = model::snap(m - drag_start, model::grid) - drag_applied;
        if (d != glm::ivec2{0, 0}) {
            move_selection(d);
            drag_applied += d;
            drag_moved = true;
        }
        if (!IsMouseDown(ImGuiMouseButton_Left)) {
            if (drag_moved) normalize();
            // a plain click on empty space still clears the selection
            else if (drag_from_bounds) selection.clear();
            mode = mode_t::idle;
        }
    } break;

    case mode_t::box_select: {
        if (!IsMouseDown(ImGuiMouseButton_Left)) {
            select(handles_in(model::make_rect(box_start, m)), true);
            mode = mode_t::idle;
        }
    } break;

    case mode_t::place_instance: {
        placing.pos = ms;
        if (!hovered) break;

        if (IsKeyPressed(ImGuiKey_Escape)) {
            mode = mode_t::idle;
        } else if (IsKeyPressed(ImGuiKey_R)) {
            placing.xf.rotation = (placing.xf.rotation + 1) % 4;
        } else if (IsMouseClicked(ImGuiMouseButton_Left)) {
            placing.id = ctx.cell.new_id();
            placing.name = unique_instance_name();
            sch.instances.push_back(placing);
            selection = {{handle::instance, placing.id}};
            ctx.modified();
            normalize();
            mode = mode_t::idle;
        }
    } break;

    case mode_t::place_port: {
        placing_port.pos = ms;
        if (!hovered) break;

        if (IsKeyPressed(ImGuiKey_Escape)) {
            mode = mode_t::idle;
        } else if (IsMouseClicked(ImGuiMouseButton_Left)) {
            auto id = ctx.cell.add_port(placing_port.dir, placing_port.pos).id;
            selection = {{handle::port, id}};
            ctx.modified();
            normalize();
            mode = mode_t::idle;
        }
    } break;

    case mode_t::wire: {
        if (!hovered) break;

        if (IsKeyPressed(ImGuiKey_Escape)) {
            normalize();
            mode = mode_t::idle;
        } else if (IsMouseClicked(ImGuiMouseButton_Left)) {
            if (!wire_started) {
                wire_start = ms;
                wire_started = true;
            } else if (ms != wire_start) {
                sch.wires.push_back({ctx.cell.new_id(), wire_start, ms});
                ctx.modified();
                wire_start = ms;
            }
        }
    } break;
    }
}

std::vector<schematic_editor::handle> schematic_editor::hit_test(glm::vec2 p)
{
    float tol = view.pixels(6.0f);

    for (auto& port : ctx.cell.ports) {
        auto d = glm::abs(p - glm::vec2(port.pos));
        if (d.x <= std::max(5.0f, tol) && d.y <= std::max(5.0f, tol)) return {{handle::port, port.id}};
    }

    // last drawn is on top
    for (auto it = sch.instances.rbegin(); it != sch.instances.rend(); ++it) {
        if (model::instance_bounds(*it, ctx.ws.find_cell(it->cell)).contains(p)) return {{handle::instance, it->id}};
    }

    // all endpoints at a junction move together
    std::vector<handle> r;
    for (auto& w : sch.wires) {
        if (glm::length(p - glm::vec2(w.a)) <= tol) r.push_back({handle::wire_a, w.id});
        if (glm::length(p - glm::vec2(w.b)) <= tol) r.push_back({handle::wire_b, w.id});
    }
    if (!r.empty()) return r;

    for (auto& w : sch.wires) {
        if (distance_to_segment(p, w.a, w.b) <= tol) return {{handle::wire_a, w.id}, {handle::wire_b, w.id}};
    }

    return {};
}

std::vector<schematic_editor::handle> schematic_editor::handles_in(model::rect rc)
{
    std::vector<handle> r;
    for (auto& p : ctx.cell.ports)
        if (rc.contains(p.pos)) r.push_back({handle::port, p.id});
    for (auto& i : sch.instances)
        if (rc.contains(i.pos)) r.push_back({handle::instance, i.id});
    for (auto& w : sch.wires) {
        if (rc.contains(w.a)) r.push_back({handle::wire_a, w.id});
        if (rc.contains(w.b)) r.push_back({handle::wire_b, w.id});
    }
    return r;
}

std::optional<model::rect> schematic_editor::selection_bounds() const
{
    std::optional<model::rect> r;
    auto add = [&](model::rect b) {
        if (r) r->expand(b);
        else r = b;
    };

    for (auto& h : selection) {
        switch (h.kind) {
        case handle::port:
            if (auto p = ctx.cell.find_port(h.id)) add(model::make_rect(glm::vec2(p->pos) - 5.0f, glm::vec2(p->pos) + 5.0f));
            break;
        case handle::instance:
            if (auto i = sch.find_instance(h.id)) add(model::instance_bounds(*i, ctx.ws.find_cell(i->cell)));
            break;
        case handle::wire_a:
            if (auto w = sch.find_wire(h.id)) add({w->a, w->a});
            break;
        case handle::wire_b:
            if (auto w = sch.find_wire(h.id)) add({w->b, w->b});
            break;
        }
    }

    if (r) {
        r->min -= float(model::grid) / 2;
        r->max += float(model::grid) / 2;
    }
    return r;
}

bool schematic_editor::is_selected(handle h) const
{
    return std::find(selection.begin(), selection.end(), h) != selection.end();
}

void schematic_editor::select(const std::vector<handle>& hs, bool add)
{
    if (!add) selection.clear();
    for (auto& h : hs)
        if (!is_selected(h)) selection.push_back(h);
}

void schematic_editor::move_selection(glm::ivec2 d)
{
    for (auto& h : selection) {
        switch (h.kind) {
        case handle::port:
            if (auto p = ctx.cell.find_port(h.id)) p->pos += d;
            break;
        case handle::instance:
            if (auto i = sch.find_instance(h.id)) i->pos += d;
            break;
        case handle::wire_a:
            if (auto w = sch.find_wire(h.id)) w->a += d;
            break;
        case handle::wire_b:
            if (auto w = sch.find_wire(h.id)) w->b += d;
            break;
        }
    }
    ctx.modified();
}

void schematic_editor::delete_selection()
{
    if (selection.empty()) return;

    for (auto& h : selection) {
        switch (h.kind) {
        case handle::port: ctx.cell.remove_port(h.id); break;
        case handle::instance: std::erase_if(sch.instances, [&](auto& i) { return i.id == h.id; }); break;
        case handle::wire_a:
        case handle::wire_b: std::erase_if(sch.wires, [&](auto& w) { return w.id == h.id; }); break;
        }
    }

    selection.clear();
    ctx.modified();
    normalize();
}

void schematic_editor::rotate_selection()
{
    bool changed = false;
    for (auto& h : selection) {
        if (h.kind != handle::instance) continue;
        if (auto i = sch.find_instance(h.id)) {
            i->xf.rotation = (i->xf.rotation + 1) % 4;
            changed = true;
        }
    }
    if (changed) {
        ctx.modified();
        normalize();
    }
}

void schematic_editor::normalize()
{
    std::vector<glm::ivec2> points;
    for (auto& i : sch.instances) {
        if (auto c = ctx.ws.find_cell(i.cell))
            for (auto& p : c->symbol.pins)
                points.push_back(model::pin_position(i, p));
    }
    for (auto& p : ctx.cell.ports)
        points.push_back(model::connection_point(p));

    // wire ids change, keep the selected wire endpoints by position
    std::set<glm::ivec2, ivec2_less> selected_points;
    for (auto& h : selection) {
        if (auto w = (h.kind == handle::wire_a || h.kind == handle::wire_b) ? sch.find_wire(h.id) : nullptr)
            selected_points.insert(h.kind == handle::wire_a ? w->a : w->b);
    }

    auto old = sch.wires;
    sch.wires = model::normalize_wires(sch.wires, points, ctx.cell.next_id);

    bool changed = old.size() != sch.wires.size();
    for (size_t i = 0; !changed && i < old.size(); i++)
        changed = old[i].a != sch.wires[i].a || old[i].b != sch.wires[i].b;
    if (changed) ctx.modified();

    std::erase_if(selection, [](auto& h) { return h.kind == handle::wire_a || h.kind == handle::wire_b; });
    for (auto& w : sch.wires) {
        if (selected_points.count(w.a)) selection.push_back({handle::wire_a, w.id});
        if (selected_points.count(w.b)) selection.push_back({handle::wire_b, w.id});
    }

    validate_selection();
}

void schematic_editor::validate_selection()
{
    std::erase_if(selection, [&](const handle& h) {
        switch (h.kind) {
        case handle::port: return ctx.cell.find_port(h.id) == nullptr;
        case handle::instance: return sch.find_instance(h.id) == nullptr;
        default: return sch.find_wire(h.id) == nullptr;
        }
    });
}

std::string schematic_editor::unique_instance_name() const
{
    for (int n = 1;; n++) {
        auto name = "U" + std::to_string(n);
        if (std::none_of(sch.instances.begin(), sch.instances.end(), [&](auto& i) { return i.name == name; }))
            return name;
    }
}

void schematic_editor::draw()
{
    auto& r = ctx.r;
    auto& vp = view.vp();

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    if (view.show_grid) r.grid(vp, view.get_zoom());

    // elements removed elsewhere (e.g. a port deleted from the properties)
    validate_selection();

    std::map<glm::ivec2, int, ivec2_less> endpoint_count;

    for (auto& w : sch.wires) {
        bool sel = is_selected({handle::wire_a, w.id}) && is_selected({handle::wire_b, w.id});
        render::draw_wire(r, vp, w, sel ? render::colors::selection : render::colors::wire);
        endpoint_count[w.a]++;
        endpoint_count[w.b]++;
    }

    // junction dots where three or more wires meet
    r.g.setColor(render::colors::wire);
    for (auto& [p, n] : endpoint_count)
        if (n >= 3) r.g.circle(vp, p, 2.0f, true);

    for (auto& i : sch.instances) {
        bool sel = is_selected({handle::instance, i.id});
        render::draw_instance(r, vp, ctx.ws, i, sel ? render::colors::selection : render::colors::symbol);
    }

    for (auto& p : ctx.cell.ports) {
        bool sel = is_selected({handle::port, p.id});
        render::draw_port(r, vp, p, sel ? render::colors::selection : render::colors::symbol);
    }

    // selected wire endpoints
    r.g.setColor(render::colors::selection);
    for (auto& h : selection) {
        if (h.kind != handle::wire_a && h.kind != handle::wire_b) continue;
        if (auto w = sch.find_wire(h.id)) {
            glm::vec2 p = h.kind == handle::wire_a ? w->a : w->b;
            r.g.rect(vp, p - 2.0f, p + 2.0f);
        }
    }

    auto ms = model::snap(view.mouse_world(), model::grid);

    switch (mode) {
    case mode_t::place_instance:
        render::draw_instance(r, vp, ctx.ws, placing, render::colors::preview);
        break;
    case mode_t::place_port:
        render::draw_port(r, vp, placing_port, render::colors::preview);
        break;
    case mode_t::wire:
        r.g.setColor(render::colors::guide);
        r.g.line(vp, {ms.x - 1000, ms.y}, {ms.x + 1000, ms.y});
        r.g.line(vp, {ms.x, ms.y - 1000}, {ms.x, ms.y + 1000});
        if (wire_started) render::draw_wire(r, vp, {model::no_id, wire_start, ms}, render::colors::preview);
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

void schematic_editor::property_panel()
{
    using namespace ImGui;

    std::vector<handle> elements;
    for (auto& h : selection)
        if (h.kind == handle::port || h.kind == handle::instance) elements.push_back(h);

    if (elements.size() == 1 && elements[0].kind == handle::instance) {
        auto i = sch.find_instance(elements[0].id);
        if (!i) return;
        auto c = ctx.ws.find_cell(i->cell);
        auto l = ctx.ws.library_of(i->cell);

        SeparatorText("Instance");
        if (input_string("Name", i->name)) ctx.modified();
        if (c)
            Text("Cell: %s / %s", l->name.c_str(), c->name.c_str());
        else
            TextColored({1, 0.3f, 0.3f, 1}, "Cell is missing");

        int rot = i->xf.rotation;
        if (Combo("Rotation", &rot, "0\0"
                                    "90\0"
                                    "180\0"
                                    "270\0")) {
            i->xf.rotation = rot;
            ctx.modified();
            normalize();
        }
        if (Checkbox("Mirror", &i->xf.mirror)) {
            ctx.modified();
            normalize();
        }
        return;
    }

    if (elements.size() == 1 && elements[0].kind == handle::port) {
        auto p = ctx.cell.find_port(elements[0].id);
        if (!p) return;

        SeparatorText("Port");
        if (input_string("Name", p->name)) ctx.modified();

        int dir = (int)p->dir;
        if (Combo("Direction", &dir, "input\0output\0inout\0")) {
            p->dir = (model::direction)dir;
            ctx.modified();
            normalize();
        }

        if (InputInt("Width", &p->width)) {
            p->width = glm::clamp(p->width, 1, 64);
            ctx.modified();
        }
        return;
    }

    SeparatorText("Cell");
    Text("%s / %s", ctx.lib.name.c_str(), ctx.cell.name.c_str());
    Text("%zu wires, %zu instances, %zu ports", sch.wires.size(), sch.instances.size(), ctx.cell.ports.size());

    if (!ctx.cell.ports.empty() && BeginTable("ports", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        TableSetupColumn("Port");
        TableSetupColumn("Direction");
        TableSetupColumn("Width");
        TableHeadersRow();
        for (auto& p : ctx.cell.ports) {
            TableNextRow();
            TableNextColumn();
            TextUnformatted(p.name.c_str());
            TableNextColumn();
            TextUnformatted(model::to_string(p.dir));
            TableNextColumn();
            Text("%d", p.width);
        }
        EndTable();
    }

    for (auto& problem : ctx.cell.check())
        TextColored({1, 0.6f, 0.2f, 1}, "%s", problem.c_str());
}

}
