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

// draws connected wire segments, each click adds a point. esc ends the wire,
// a second esc leaves the tool.
class wire_tool : public tool<schematic_editor> {
public:
    void update(schematic_editor& e, bool hovered) override
    {
        using namespace ImGui;
        if (!hovered) return;

        auto ms = model::snap(e.view.mouse_world(), model::grid);

        if (IsKeyPressed(ImGuiKey_Escape)) {
            if (started)
                started = false;
            else
                done = true;
        } else if (IsMouseClicked(ImGuiMouseButton_Left)) {
            if (!started) {
                start = ms;
                started = true;
            } else if (ms != start) {
                e.sch.wires.push_back({e.ctx.cell.new_id(), start, ms});
                e.ctx.modified();
                // every segment is an undo step
                e.normalize();
                start = ms;
            }
        }
    }

    void draw(schematic_editor& e) override
    {
        auto& r = e.ctx.r;
        auto& vp = e.view.vp();
        auto ms = model::snap(e.view.mouse_world(), model::grid);

        r.g.setColor(render::colors::guide);
        r.g.line(vp, {ms.x - 1000, ms.y}, {ms.x + 1000, ms.y});
        r.g.line(vp, {ms.x, ms.y - 1000}, {ms.x, ms.y + 1000});
        if (started) render::draw_wire(r, vp, {model::no_id, start, ms}, render::colors::preview);
    }

private:
    bool started = false;
    glm::ivec2 start{0, 0};
};

// places a port at the click, shift keeps placing
class port_tool : public tool<schematic_editor> {
public:
    explicit port_tool(model::direction d) { preview.dir = d; }

    void update(schematic_editor& e, bool hovered) override
    {
        using namespace ImGui;

        preview.pos = model::snap(e.view.mouse_world(), model::grid);
        preview.name = e.ctx.cell.unique_port_name(preview.dir == model::direction::input ? "in" : "out");
        if (!hovered) return;

        if (IsKeyPressed(ImGuiKey_Escape)) {
            done = true;
        } else if (IsMouseClicked(ImGuiMouseButton_Left)) {
            auto& p = e.ctx.cell.add_port(preview.dir, preview.pos);
            e.selection = {{schematic_editor::handle::port, p.id}};
            e.ctx.modified();
            e.normalize();
            done = !GetIO().KeyShift;
        }
    }

    void draw(schematic_editor& e) override
    {
        render::draw_port(e.ctx.r, e.view.vp(), preview, render::colors::preview);
    }

private:
    model::port preview;
};

// places instances of a cell, r rotates, shift keeps placing
class instance_tool : public tool<schematic_editor> {
public:
    explicit instance_tool(model::id cell) { preview.cell = cell; }

    void update(schematic_editor& e, bool hovered) override
    {
        using namespace ImGui;

        preview.pos = model::snap(e.view.mouse_world(), model::grid);
        if (!hovered) return;

        if (IsKeyPressed(ImGuiKey_Escape)) {
            done = true;
        } else if (IsKeyPressed(ImGuiKey_R)) {
            preview.xf.rotation = (preview.xf.rotation + 1) % 4;
        } else if (IsMouseClicked(ImGuiMouseButton_Left)) {
            auto i = preview;
            i.id = e.ctx.cell.new_id();
            i.name = e.unique_instance_name();
            e.sch.instances.push_back(i);
            e.selection = {{schematic_editor::handle::instance, i.id}};
            e.ctx.modified();
            e.normalize();
            done = !GetIO().KeyShift;
        }
    }

    void draw(schematic_editor& e) override
    {
        render::draw_instance(e.ctx.r, e.view.vp(), e.ctx.ws, preview, render::colors::preview);
    }

private:
    model::instance preview;
};

}

schematic_editor::schematic_editor(context c)
    : ctx(c)
    , sch(*c.cell.get_schematic())
    , active(std::make_unique<select_tool<schematic_editor>>())
{
}

void schematic_editor::set_tool(tool_kind k, std::unique_ptr<tool<schematic_editor>> t)
{
    if (busy()) return;
    kind = k;
    active = std::move(t);
}

void schematic_editor::set_tool(tool_kind k)
{
    switch (k) {
    case tool_kind::select: set_tool(k, std::make_unique<select_tool<schematic_editor>>()); break;
    case tool_kind::wire:
        selection.clear();
        set_tool(k, std::make_unique<wire_tool>());
        break;
    case tool_kind::input_port: set_tool(k, std::make_unique<port_tool>(model::direction::input)); break;
    case tool_kind::output_port: set_tool(k, std::make_unique<port_tool>(model::direction::output)); break;
    // needs a cell, see begin_place()
    case tool_kind::place_instance: break;
    }
}

void schematic_editor::begin_place(model::id cell)
{
    if (ctx.ws.would_create_cycle(ctx.cell.id, cell)) return;
    set_tool(tool_kind::place_instance, std::make_unique<instance_tool>(cell));
}

void schematic_editor::cell_replaced()
{
    validate_selection();
}

void schematic_editor::process_inputs(bool hovered)
{
    using namespace ImGui;

    if (hovered) view.handle_view_input();

    // shortcuts with ctrl belong to the app (undo, save)
    if (hovered && !busy() && !GetIO().KeyCtrl) {
        if (IsKeyPressed(ImGuiKey_G)) view.show_grid = !view.show_grid;

        if (IsKeyPressed(ImGuiKey_W))
            set_tool(tool_kind::wire);
        else if (IsKeyPressed(ImGuiKey_I))
            set_tool(tool_kind::input_port);
        else if (IsKeyPressed(ImGuiKey_O))
            set_tool(tool_kind::output_port);
        else if (kind == tool_kind::select && IsKeyPressed(ImGuiKey_R))
            rotate_selection();
    }

    active->update(*this, hovered);
    if (active->done) set_tool(tool_kind::select);
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

    auto next_id = ctx.cell.next_id;
    auto wires = model::normalize_wires(sch.wires, points, next_id);

    // all wires get new ids, keep the old ones if nothing changed so there is
    // no undo step without a visible change
    bool changed = wires.size() != sch.wires.size();
    for (size_t i = 0; !changed && i < wires.size(); i++)
        changed = wires[i].a != sch.wires[i].a || wires[i].b != sch.wires[i].b;
    if (changed) {
        sch.wires = std::move(wires);
        ctx.cell.next_id = next_id;
        ctx.modified();
    }

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

    active->draw(*this);
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
