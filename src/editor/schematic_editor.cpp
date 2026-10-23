#include "schematic_editor.h"
#include "../render/live.h"
#include "../sim/numbers.h"
#include "../ui/theme.h"
#include "bundle_ui.h"
#include "number_ui.h"
#include "param_editor.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cctype>
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

// draws connected wire segments, each click adds a point. esc or a right
// click (not a right drag, that pans) ends the wire
// and the tool.
class wire_tool : public tool<schematic_editor> {
public:
    void update(schematic_editor& e, bool hovered) override
    {
        using namespace ImGui;
        if (!hovered) return;

        auto ms = model::snap(e.view.mouse_world(), model::grid);

        if (IsKeyPressed(ImGuiKey_Escape) || right_clicked()) {
            // the segments drawn so far are already in the schematic
            started = false;
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
        auto ms = glm::vec2(model::snap(e.view.mouse_world(), model::grid));
        auto& c = render::colors();

        r.line(ms - glm::vec2{1000, 0}, ms + glm::vec2{1000, 0}, c.guide, r.pixel());
        r.line(ms - glm::vec2{0, 1000}, ms + glm::vec2{0, 1000}, c.guide, r.pixel());
        if (started) render::draw_wire(r, {model::no_id, start, glm::ivec2(ms)}, c.preview);
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
        render::draw_port(e.ctx.r, preview, render::colors().preview);
    }

private:
    model::port preview;
};

// places instances of a cell, r rotates (shift+r the other way), m mirrors,
// shift keeps placing
class instance_tool : public tool<schematic_editor> {
public:
    explicit instance_tool(model::id cell) { preview.cell = cell; }

    void update(schematic_editor& e, bool hovered) override
    {
        using namespace ImGui;

        preview.pos = model::snap(e.view.mouse_world(), model::grid);
        if (!hovered) return;

        int turns;
        bool mirror;
        if (IsKeyPressed(ImGuiKey_Escape)) {
            done = true;
        } else if (placing_transform_shortcut(turns, mirror)) {
            preview.xf = preview.xf.then(turns, mirror);
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
        render::draw_instance(e.ctx.r, e.ctx.ws, preview, render::colors().preview);
        e.draw_live(preview, 0);
    }

private:
    model::instance preview;
};

// copied elements following the mouse until a click places them. r
// rotates them about the mouse (shift+r the other way), m mirrors.
class paste_tool : public tool<schematic_editor> {
public:
    explicit paste_tool(model::cell part)
        : part(std::move(part))
    {
    }

    void update(schematic_editor& e, bool hovered) override
    {
        using namespace ImGui;

        offset = model::snap(e.view.mouse_world(), model::grid);
        if (!hovered) return;

        int turns;
        bool mirror;
        if (IsKeyPressed(ImGuiKey_Escape)) {
            done = true;
        } else if (placing_transform_shortcut(turns, mirror)) {
            transform(turns, mirror);
        } else if (IsMouseClicked(ImGuiMouseButton_Left)) {
            e.insert(part, offset);
            done = !GetIO().KeyShift;
        }
    }

    void draw(schematic_editor& e) override
    {
        auto& r = e.ctx.r;
        auto c = render::colors().preview;
        auto sch = part.get_schematic();
        for (auto& w : sch->wires)
            render::draw_wire(r, {model::no_id, w.a + offset, w.b + offset}, c);
        for (auto i : sch->instances) {
            i.pos += offset;
            render::draw_instance(r, e.ctx.ws, i, c);
            e.draw_live(i, 0);
        }
        for (auto p : part.ports) {
            p.pos += offset;
            // like the instances: nothing is connected yet, the pin shows its dot
            render::draw_port(r, p, c, false, p.bundle != model::no_id ? e.ctx.ws.bundle_label(p.bundle, p.flipped) : std::string());
        }
    }

private:
    // about the origin, which follows the mouse
    void transform(int turns, bool mirror)
    {
        auto turn = [&](glm::ivec2 p) { return turn_about(p, {0, 0}, turns, mirror); };
        for (auto& p : part.ports)
            p.pos = turn(p.pos);
        auto sch = part.get_schematic();
        for (auto& i : sch->instances) {
            i.pos = turn(i.pos);
            i.xf = i.xf.then(turns, mirror);
        }
        for (auto& w : sch->wires) {
            w.a = turn(w.a);
            w.b = turn(w.b);
        }
    }

    model::cell part;
    glm::ivec2 offset{0, 0};
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
    // need a cell or something copied, see begin_place() and begin_paste()
    case tool_kind::place_instance:
    case tool_kind::paste: break;
    }
}

void schematic_editor::begin_place(model::id cell)
{
    if (ctx.ws.would_create_cycle(ctx.cell.id, cell) || busy()) return;
    set_tool(tool_kind::place_instance, std::make_unique<instance_tool>(cell));
    placing_cell = cell;
}

std::optional<model::cell> schematic_editor::copy_selection() const
{
    auto bounds = selection_bounds();
    if (!bounds) return std::nullopt;
    return copy_of(selection, model::snap((bounds->min + bounds->max) / 2.0f, model::grid));
}

std::optional<model::cell> schematic_editor::copy_of(const std::vector<handle>& handles, glm::ivec2 origin) const
{
    auto chosen = [&](handle h) { return std::find(handles.begin(), handles.end(), h) != handles.end(); };

    model::cell part;
    part.name = ctx.cell.name;
    part.impl = model::schematic{};
    auto& s = *part.get_schematic();

    for (auto& p : ctx.cell.ports) {
        if (!chosen({handle::port, p.id})) continue;
        part.ports.push_back(p);
        part.ports.back().pos -= origin;
    }
    for (auto& i : sch.instances) {
        if (!chosen({handle::instance, i.id})) continue;
        s.instances.push_back(i);
        s.instances.back().pos -= origin;
    }
    for (auto& w : sch.wires) {
        if (!chosen({handle::wire_a, w.id}) || !chosen({handle::wire_b, w.id})) continue;
        s.wires.push_back({w.id, w.a - origin, w.b - origin});
    }

    if (part.ports.empty() && s.instances.empty() && s.wires.empty()) return std::nullopt;
    return part;
}

void schematic_editor::duplicate_hovered()
{
    auto m = view.mouse_world();
    auto hit = hit_test(m);
    if (hit.empty()) return;
    // the first element under the mouse, a wire end means the whole segment
    std::vector<handle> hs{hit[0]};
    if (hit[0].kind == handle::wire_a || hit[0].kind == handle::wire_b) {
        auto w = sch.find_wire(hit[0].id);
        if (!w) return;
        hs = {{handle::wire_a, w->id}, {handle::wire_b, w->id}};
    }

    // an instance or port follows the mouse by its origin like a new one,
    // a wire by the point under the mouse
    glm::ivec2 origin = model::snap(m, model::grid);
    if (hs[0].kind == handle::instance) origin = sch.find_instance(hs[0].id)->pos;
    if (hs[0].kind == handle::port) origin = ctx.cell.find_port(hs[0].id)->pos;
    if (auto part = copy_of(hs, origin)) begin_paste(std::move(*part));
}

void schematic_editor::begin_paste(model::cell part)
{
    if (!part.get_schematic()) return;
    auto& s = *part.get_schematic();
    std::erase_if(s.instances, [&](auto& i) { return ctx.ws.would_create_cycle(ctx.cell.id, i.cell); });
    selection.clear();
    set_tool(tool_kind::paste, std::make_unique<paste_tool>(std::move(part)));
}

void schematic_editor::insert(const model::cell& part, glm::ivec2 offset)
{
    selection.clear();

    for (auto p : part.ports) {
        p.id = ctx.cell.new_id();
        p.pos += offset;
        if (std::any_of(ctx.cell.ports.begin(), ctx.cell.ports.end(), [&](auto& q) { return q.name == p.name; })) {
            // "in3" becomes the next free "in<n>"
            auto base = p.name;
            while (!base.empty() && std::isdigit((unsigned char)base.back()))
                base.pop_back();
            p.name = ctx.cell.unique_port_name(base);
        }
        ctx.cell.ports.push_back(p);
        selection.push_back({handle::port, p.id});
    }

    auto s = part.get_schematic();
    for (auto i : s->instances) {
        if (ctx.ws.would_create_cycle(ctx.cell.id, i.cell)) continue;
        i.id = ctx.cell.new_id();
        i.pos += offset;
        if (i.name.empty() || std::any_of(sch.instances.begin(), sch.instances.end(), [&](auto& j) { return j.name == i.name; }))
            i.name = unique_instance_name();
        sch.instances.push_back(i);
        selection.push_back({handle::instance, i.id});
    }

    for (auto w : s->wires) {
        w.id = ctx.cell.new_id();
        w.a += offset;
        w.b += offset;
        sch.wires.push_back(w);
        selection.push_back({handle::wire_a, w.id});
        selection.push_back({handle::wire_b, w.id});
    }

    ctx.modified();
    normalize();
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
        if (IsKeyPressed(ImGuiKey_G)) ctx.r.show_grid = !ctx.r.show_grid;

        if (IsKeyPressed(ImGuiKey_W))
            set_tool(tool_kind::wire);
        else if (IsKeyPressed(ImGuiKey_I))
            set_tool(tool_kind::input_port);
        else if (IsKeyPressed(ImGuiKey_O))
            set_tool(tool_kind::output_port);
        else if (IsKeyPressed(ImGuiKey_Q) && kind == tool_kind::select)
            duplicate_hovered();
    }

    int turns;
    bool mirror;
    if (hovered && !busy() && kind == tool_kind::select && transform_shortcut(turns, mirror)) transform_selection(turns, mirror);

    active->update(*this, hovered);
    if (active->done) set_tool(tool_kind::select);

    if (hovered && kind == tool_kind::select) hover_tooltip();
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
        if (model::instance_bounds(*it, ctx.ws.interface_of(*it).symbol).contains(p)) return {{handle::instance, it->id}};
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

model::id schematic_editor::wire_at(glm::vec2 p) const
{
    float tol = view.pixels(6.0f);
    for (auto& w : sch.wires)
        if (distance_to_segment(p, w.a, w.b) <= tol) return w.id;
    return model::no_id;
}

namespace {

// what a tooltip shows of a bundle: its name and its fields
struct bundle_info {
    std::string label;
    model::bundle_layout layout;
};

std::optional<bundle_info> bundle_info_of(const model::workspace& ws, model::id bundle, bool flipped)
{
    if (bundle == model::no_id) return std::nullopt;
    return bundle_info{ws.bundle_label(bundle, flipped), ws.layout_of(bundle, flipped)};
}

// name, direction, width and description of a port, the value if there is
// one. bundle ports list their fields.
void port_tooltip(const std::string& name, const model::port& p, const sim::value* v, const std::optional<bundle_info>& b)
{
    using namespace ImGui;

    BeginTooltip();
    PushTextWrapPos(GetFontSize() * 24);
    TextUnformatted(name.c_str());
    SameLine();
    if (b)
        TextDisabled(p.width == 1 ? "%s, %d bit" : "%s, %d bits", b->label.c_str(), p.width);
    else if (p.width == 0)
        TextDisabled("%s, takes the width of its net", model::to_string(p.dir));
    else
        TextDisabled(p.width == 1 ? "%s, %d bit" : "%s, %d bits", model::to_string(p.dir), p.width);
    if (!p.description.empty()) TextUnformatted(p.description.c_str());
    if (b) {
        bundle_fields_table(b->layout, v);
    } else if (v) {
        auto c = render::value_color(*v);
        auto str = sim::format_number(*v, p.format);
        // the number too, unless the format already shows it
        bool decimal = p.format.kind == model::number_format::unsigned_dec || p.format.kind == model::number_format::signed_dec;
        if (p.width > 1 && p.width <= 64 && v->fully_known() && !decimal) str += " (" + std::to_string(v->to_u64()) + ")";
        TextColored({c.r, c.g, c.b, c.a}, "%s", str.c_str());
    }
    PopTextWrapPos();
    EndTooltip();
}

}

schematic_editor::hover_target schematic_editor::hover_at(glm::vec2 m) const
{
    float tol = std::max(3.0f, view.pixels(6.0f));

    // the cell's own port connectors
    for (int k = 0; k < (int)ctx.cell.ports.size(); k++) {
        auto& p = ctx.cell.ports[k];
        bool near = glm::length(m - glm::vec2(p.pos)) <= std::max(6.0f, tol) || glm::length(m - glm::vec2(model::connection_point(p))) <= tol;
        if (!near) continue;
        std::optional<sim::value> v;
        if (sim) v = sim->port_value(*sim_scope, k);
        auto b = bundle_info_of(ctx.ws, p.bundle, p.flipped);
        return {"port " + std::to_string(p.id), [p, v, b] { port_tooltip(p.name, p, v ? &*v : nullptr, b); }};
    }

    // pins of instances
    for (int i = (int)sch.instances.size() - 1; i >= 0; i--) {
        auto& inst = sch.instances[i];
        auto iface = ctx.ws.interface_of(inst);
        if (!iface) continue;
        for (auto& pin : iface.symbol->pins) {
            if (glm::length(m - glm::vec2(model::pin_position(inst, pin))) > tol) continue;
            for (int k = 0; k < (int)iface.ports->size(); k++) {
                auto& p = (*iface.ports)[k];
                if (p.id != pin.port) continue;
                std::optional<sim::value> v;
                if (sim) {
                    v = sim::floating(p.width);
                    auto& nets = sim_scope->nets->instance_nets;
                    int local = i < (int)nets.size() && k < (int)nets[i].size() ? nets[i][k] : -1;
                    if (local >= 0) v = sim->net_value(sim_scope->flat_net[local]);
                }
                auto name = inst.name + "." + p.name;
                auto b = bundle_info_of(ctx.ws, p.bundle, p.flipped);
                return {"pin " + std::to_string(inst.id) + " " + std::to_string(p.id), [name, p, v, b] { port_tooltip(name, p, v ? &*v : nullptr, b); }};
            }
        }
    }

    // wires, by net so moving along connected wires keeps the tooltip
    auto w = wire_at(m);
    if (w == model::no_id) return {};

    if (sim) {
        int n = sim->net_of_wire(*sim_scope, w);
        if (n < 0) return {};
        int width = sim->net_width(n);
        auto v = sim->net_value(n);
        int local = sim_scope->nets->net_of_wire(w);
        if (auto b = local >= 0 ? bundle_info_of(ctx.ws, sim_scope->nets->nets[local].bundle, false) : std::nullopt) {
            return {"net " + std::to_string(n), [b, v] {
                        using namespace ImGui;
                        BeginTooltip();
                        Text("%s, %d bits", b->label.c_str(), v.width());
                        bundle_fields_table(b->layout, &v);
                        EndTooltip();
                    }};
        }
        return {"net " + std::to_string(n), [width, v] {
                    using namespace ImGui;
                    if (width > 1 && width <= 64 && v.fully_known())
                        SetTooltip("%d bits: %s (%llu)", width, sim::to_string(v).c_str(), (unsigned long long)v.to_u64());
                    else if (width > 1)
                        SetTooltip("%d bits: %s", width, sim::to_string(v).c_str());
                    else
                        SetTooltip("%s", width == 0 ? "not connected to a port" : sim::to_string(v).c_str());
                }};
    }

    int n = nets.net_of_wire(w);
    if (n < 0) return {};
    auto net = nets.nets[n];
    if (auto b = !net.width_error && !net.type_error ? bundle_info_of(ctx.ws, net.bundle, false) : std::nullopt) {
        return {"net " + std::to_string(n), [b, net] {
                    using namespace ImGui;
                    BeginTooltip();
                    Text("%s, %d bits", b->label.c_str(), net.width);
                    bundle_fields_table(b->layout);
                    EndTooltip();
                }};
    }
    return {"net " + std::to_string(n), [net] {
                using namespace ImGui;
                if (net.width_error)
                    SetTooltip("ports of different widths are connected");
                else if (net.type_error)
                    SetTooltip("ports of different types are connected");
                else if (net.width == 0)
                    SetTooltip("not connected to a port");
                else
                    SetTooltip(net.width == 1 ? "%d bit" : "%d bits", net.width);
            }};
}

void schematic_editor::hover_tooltip()
{
    using namespace ImGui;

    auto t = busy() ? hover_target{} : hover_at(view.mouse_world());
    if (t.key != hover.key) {
        hover = {t.key, GetTime(), std::nullopt};
        return;
    }
    if (t.key.empty() || GetTime() - hover.since < tooltip_delay) return;

    // stays where it first appeared while the mouse is on the same thing
    if (!hover.pos) {
        auto m = GetMousePos();
        hover.pos = ImVec2{m.x + 16, m.y + 16};
    }
    SetNextWindowPos(*hover.pos);
    t.show();
}

int schematic_editor::net_at(glm::vec2 m) const
{
    auto& nl = sim ? *sim_scope->nets : nets;
    float tol = std::max(3.0f, view.pixels(6.0f));

    for (int k = 0; k < (int)ctx.cell.ports.size() && k < (int)nl.port_nets.size(); k++) {
        auto& p = ctx.cell.ports[k];
        if (glm::length(m - glm::vec2(p.pos)) <= std::max(6.0f, tol) || glm::length(m - glm::vec2(model::connection_point(p))) <= tol) return nl.port_nets[k];
    }

    for (int i = (int)sch.instances.size() - 1; i >= 0; i--) {
        if (i >= (int)nl.instance_nets.size()) continue;
        auto& inst = sch.instances[i];
        auto iface = ctx.ws.interface_of(inst);
        if (!iface) continue;
        for (auto& pin : iface.symbol->pins) {
            if (glm::length(m - glm::vec2(model::pin_position(inst, pin))) > tol) continue;
            for (int k = 0; k < (int)iface.ports->size(); k++)
                if ((*iface.ports)[k].id == pin.port && k < (int)nl.instance_nets[i].size()) return nl.instance_nets[i][k];
        }
        // a tunnel stands for its net
        if (iface.tunnel && !iface.tunnel->empty() && !nl.instance_nets[i].empty() && model::instance_bounds(inst, iface.symbol).contains(m))
            return nl.instance_nets[i][0];
    }

    auto w = wire_at(m);
    return w == model::no_id ? -1 : nl.net_of_wire(w);
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
            if (auto i = sch.find_instance(h.id)) add(model::instance_bounds(*i, ctx.ws.interface_of(*i).symbol));
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

void schematic_editor::transform_selection(int turns, bool mirror)
{
    if (selection.empty()) return;
    glm::ivec2 pivot;
    if (selection.size() == 1 && selection[0].kind == handle::instance) {
        auto i = sch.find_instance(selection[0].id);
        if (!i) return;
        pivot = i->pos;
    } else if (auto b = selection_bounds()) {
        pivot = pivot_of(*b);
    } else {
        return;
    }

    auto turn = [&](glm::ivec2 p) { return turn_about(p, pivot, turns, mirror); };
    for (auto& h : selection) {
        switch (h.kind) {
        case handle::port:
            if (auto p = ctx.cell.find_port(h.id)) p->pos = turn(p->pos);
            break;
        case handle::instance:
            if (auto i = sch.find_instance(h.id)) {
                i->pos = turn(i->pos);
                i->xf = i->xf.then(turns, mirror);
            }
            break;
        case handle::wire_a:
            if (auto w = sch.find_wire(h.id)) w->a = turn(w->a);
            break;
        case handle::wire_b:
            if (auto w = sch.find_wire(h.id)) w->b = turn(w->b);
            break;
        }
    }
    ctx.modified();
    normalize();
}

void schematic_editor::normalize()
{
    std::vector<glm::ivec2> points;
    for (auto& i : sch.instances) {
        if (auto iface = ctx.ws.interface_of(i))
            for (auto& p : iface.symbol->pins)
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

void schematic_editor::draw_live(const model::instance& i, size_t index)
{
    auto cell = ctx.ws.find_cell(i.cell);
    auto prim = cell ? ctx.ws.primitive_of(*cell) : nullptr;
    if (!prim) return;
    auto iface = ctx.ws.interface_of(i);
    if (!iface) return;

    // the values at the ports, none while editing
    std::vector<sim::value> pins;
    auto params = model::resolve_params(prim->params(), i.params);
    const bool* shown = nullptr;
    if (sim) {
        auto& local = sim_scope->nets->instance_nets;
        for (size_t p = 0; p < iface.ports->size(); p++) {
            int n = index < local.size() && p < local[index].size() ? local[index][p] : -1;
            pins.push_back(n < 0 ? sim::floating((*iface.ports)[p].width) : sim->net_value(sim_scope->flat_net[n]));
        }

        // displays: a segment lit at any time since the last update (its
        // share of the time is above 0) shows fully lit, and stays so for the
        // persistence time, so multiplexed digits are all visible
        int k = index < sim_scope->components.size() ? sim_scope->components[index] : -1;
        if (auto view = k >= 0 ? sim->view(k) : nullptr) {
            auto& g = ghosts[{sim_scope, index}];
            auto now = std::chrono::steady_clock::now();
            auto it = params.find("persistence");
            auto persistence = std::chrono::milliseconds(it != params.end() && std::get_if<int64_t>(&it->second) ? std::get<int64_t>(it->second) : 0);
            for (size_t s = 0; s < 8 && s < view->size(); s++) {
                bool lit = (*view)[s] > 0.0f;
                if (lit) g.lit_until[s] = now + persistence;
                g.shown[s] = lit || now < g.lit_until[s];
            }
            shown = g.shown;
        }
    }
    render::draw_live(ctx.r, prim->kind(), params, i, *iface.symbol, *iface.ports, pins, shown);
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
    auto& c = render::colors();
    r.begin(view.vp());

    // elements removed elsewhere (e.g. a port deleted from the properties)
    validate_selection();

    r.draw_origin();

    if (!sim) nets = netlist::extract(ctx.ws, ctx.cell);
    if (!sim) ghosts.clear();

    std::map<glm::ivec2, int, ivec2_less> endpoint_count;
    std::map<glm::ivec2, glm::vec4, ivec2_less> junction_color;
    std::set<glm::ivec2, ivec2_less> bus_junction;

    // the local nets know the bundle types, also while simulating
    auto& local_nets = sim ? *sim_scope->nets : nets;
    std::set<glm::ivec2, ivec2_less> bundle_junction;

    // the highlighted net glows under its wires
    if (highlight_net >= 0) {
        auto glow = c.selection;
        glow.a *= 0.45f;
        for (auto& w : sch.wires) {
            if (local_nets.net_of_wire(w.id) != highlight_net) continue;
            bool bundle = local_nets.nets[highlight_net].bundle != model::no_id;
            float width = r.wire_line(local_nets.nets[highlight_net].width > 1, bundle);
            r.line(w.a, w.b, glow, width + 2.0f);
        }
    }

    for (auto& w : sch.wires) {
        bool sel = is_selected({handle::wire_a, w.id}) && is_selected({handle::wire_b, w.id});
        bool bus = false;
        int local = local_nets.net_of_wire(w.id);
        bool bundle = local >= 0 && local_nets.nets[local].bundle != model::no_id;
        // buses and bundles have their own colors while editing
        glm::vec4 base = c.wire;
        glm::vec4 color;
        if (sim) {
            int n = sim->net_of_wire(*sim_scope, w.id);
            color = n < 0 ? c.floating : render::value_color(sim->net_value(n), bundle);
            bus = n >= 0 && sim->net_width(n) > 1;
        } else {
            if (local >= 0) bus = nets.nets[local].width > 1;
            base = bundle ? c.bundle_wire : bus ? c.bus_wire
                                                : c.wire;
            color = sel ? c.selection : base;
            if (local >= 0 && (nets.nets[local].width_error || nets.nets[local].type_error) && !sel) color = c.width_error;
        }
        render::draw_wire(r, w, color, bus, bundle);
        endpoint_count[w.a]++;
        endpoint_count[w.b]++;
        junction_color[w.a] = junction_color[w.b] = sim ? color : base;
        if (bus) bus_junction.insert({w.a, w.b});
        if (bundle) bundle_junction.insert({w.a, w.b});
    }

    // junction dots where three or more wires meet
    for (auto& [p, n] : endpoint_count) {
        if (n < 3) continue;
        // as wide as the wires
        float radius = 2.0f;
        if (r.thick_buses && bundle_junction.count(p))
            radius = render::bundle_width * 0.8f;
        else if (r.thick_buses && bus_junction.count(p))
            radius = 3.0f;
        r.circle(p, radius, junction_color[p]);
    }

    // pins without a wire or another pin get a circle
    auto& instance_nets = sim ? sim_scope->nets->instance_nets : nets.instance_nets;
    for (size_t k = 0; k < sch.instances.size(); k++) {
        auto& i = sch.instances[k];
        bool sel = is_selected({handle::instance, i.id}) || (sim && (int)k == sim_selected);
        // tunnels of the highlighted net
        if (highlight_net >= 0 && k < instance_nets.size() && !instance_nets[k].empty() && instance_nets[k][0] == highlight_net) {
            auto iface = ctx.ws.interface_of(i);
            sel = sel || (iface.tunnel && !iface.tunnel->empty());
        }
        render::draw_instance(r, ctx.ws, i, sel ? c.selection : c.symbol, k < instance_nets.size() ? &instance_nets[k] : nullptr);
        draw_live(i, k);
    }

    for (int k = 0; k < (int)ctx.cell.ports.size(); k++) {
        auto& p = ctx.cell.ports[k];
        auto& port_nets = sim ? sim_scope->nets->port_nets : nets.port_nets;
        bool connected = k < (int)port_nets.size() && port_nets[k] >= 0;
        bool sel = is_selected({handle::port, p.id}) || (highlight_net >= 0 && connected && port_nets[k] == highlight_net);
        render::draw_port(r, p, sel ? c.selection : c.symbol, connected, p.bundle != model::no_id ? ctx.ws.bundle_label(p.bundle, p.flipped) : std::string());
        if (sim) render::draw_port_value(r, p, sim->port_value(*sim_scope, k));
    }

    // selected wire endpoints
    for (auto& h : selection) {
        if (h.kind != handle::wire_a && h.kind != handle::wire_b) continue;
        if (auto w = sch.find_wire(h.id)) {
            glm::vec2 p = h.kind == handle::wire_a ? w->a : w->b;
            r.rect_outline(p - 2.0f, p + 2.0f, c.selection, 0.6f, 0.5f);
        }
    }

    if (!sim) active->draw(*this);
    r.end();
}

void schematic_editor::selection_properties(const std::vector<handle>& elements)
{
    using namespace ImGui;

    std::vector<model::instance*> insts;
    std::vector<model::port*> ports;
    for (auto& h : elements) {
        if (h.kind == handle::instance) {
            if (auto i = sch.find_instance(h.id)) insts.push_back(i);
        } else if (auto p = ctx.cell.find_port(h.id)) {
            ports.push_back(p);
        }
    }

    SeparatorText("Selection");
    Text("%zu instances, %zu ports", insts.size(), ports.size());
    TextDisabled("changes apply to all of them");

    if (!insts.empty()) {
        SeparatorText("Instances");

        static const char* const angles[] = {"0", "90", "180", "270"};
        bool same_rot = std::all_of(insts.begin(), insts.end(), [&](auto i) { return i->xf.rotation == insts[0]->xf.rotation; });
        if (BeginCombo("Rotation", same_rot ? angles[insts[0]->xf.rotation & 3] : "mixed")) {
            for (int r = 0; r < 4; r++) {
                if (Selectable(angles[r], same_rot && insts[0]->xf.rotation == r)) {
                    for (auto i : insts)
                        i->xf.rotation = r;
                    ctx.modified();
                    normalize();
                }
            }
            EndCombo();
        }

        bool same_mirror = std::all_of(insts.begin(), insts.end(), [&](auto i) { return i->xf.mirror == insts[0]->xf.mirror; });
        bool m = same_mirror && insts[0]->xf.mirror;
        PushItemFlag(ImGuiItemFlags_MixedValue, !same_mirror);
        if (Checkbox("Mirror", &m)) {
            for (auto i : insts)
                i->xf.mirror = m;
            ctx.modified();
            normalize();
        }
        PopItemFlag();

        // the parameters every instance has, with the same definition
        auto defs = ctx.ws.params_of(*insts[0]);
        for (size_t k = 1; k < insts.size(); k++) {
            auto other = ctx.ws.params_of(*insts[k]);
            std::erase_if(defs, [&](auto& d) { return std::find(other.begin(), other.end(), d) == other.end(); });
        }
        if (!defs.empty()) SeparatorText("Common parameters");
        for (auto& d : defs) {
            auto value_of = [&](const model::instance* i) {
                auto it = i->params.find(d.name);
                return it != i->params.end() ? it->second : d.def;
            };
            bool same = std::all_of(insts.begin(), insts.end(), [&](auto i) { return value_of(i) == value_of(insts[0]); });
            // edited as the first instance's value
            model::param_values one;
            if (auto it = insts[0]->params.find(d.name); it != insts[0]->params.end()) one[d.name] = it->second;
            PushID(d.name.c_str());
            PushItemFlag(ImGuiItemFlags_MixedValue, !same);
            bool changed = edit_params({d}, one, ctx.lib.dir, &ctx.ws);
            PopItemFlag();
            if (!same) {
                SameLine();
                TextDisabled("(mixed)");
            }
            PopID();
            if (changed) {
                for (auto i : insts) {
                    if (auto it = one.find(d.name); it != one.end()) i->params[d.name] = it->second;
                    else i->params.erase(d.name);
                }
                ctx.modified();
                normalize();
            }
        }
    }

    if (!ports.empty()) {
        SeparatorText("Ports");
        static const char* const dirs[] = {"input", "output", "inout"};
        bool same_dir = std::all_of(ports.begin(), ports.end(), [&](auto p) { return p->dir == ports[0]->dir; });
        if (BeginCombo("Direction", same_dir ? dirs[(int)ports[0]->dir] : "mixed")) {
            for (int d = 0; d < 3; d++) {
                if (Selectable(dirs[d], same_dir && (int)ports[0]->dir == d)) {
                    for (auto p : ports)
                        p->dir = (model::direction)d;
                    ctx.modified();
                    normalize();
                }
            }
            EndCombo();
        }

        bool same_width = std::all_of(ports.begin(), ports.end(), [&](auto p) { return p->width == ports[0]->width; });
        int w = ports[0]->width;
        if (InputInt("Width", &w)) {
            w = glm::clamp(w, 1, model::max_width);
            // bundle ports have the width of their type
            for (auto p : ports)
                if (p->bundle == model::no_id) p->width = w;
            ctx.modified();
        }
        if (!same_width) {
            SameLine();
            TextDisabled("(mixed)");
        }
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
        if (c) {
            Text("Cell: %s / %s", l->name.c_str(), c->name.c_str());
            if (auto prim = ctx.ws.primitive_of(*c); prim && !prim->description().empty()) TextDisabled("%s", prim->description().c_str());
        } else {
            TextColored(ui::color(ui::color_id::error), "Cell is missing");
        }

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

        if (auto defs = ctx.ws.params_of(*i); !defs.empty()) {
            SeparatorText("Parameters");
            if (edit_params(defs, i->params, ctx.lib.dir, &ctx.ws)) {
                ctx.modified();
                // pins may have moved or appeared
                normalize();
            }
            auto problem = ctx.ws.check_instance(*i);
            if (!problem.empty()) TextColored(ui::color(ui::color_id::warning), "%s", problem.c_str());
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

        // plain bits or a bundle type
        auto type = p->bundle == model::no_id ? std::string("bits") : ctx.ws.bundle_label(p->bundle);
        if (BeginCombo("Type", type.c_str())) {
            if (Selectable("bits", p->bundle == model::no_id) && p->bundle != model::no_id) {
                p->bundle = model::no_id;
                p->flipped = false;
                ctx.modified();
            }
            for (auto& l : ctx.ws.libraries) {
                if (l->bundles.empty()) continue;
                SeparatorText(l->name.c_str());
                for (auto& b : l->bundles) {
                    PushID(b.get());
                    if (Selectable(b->name.c_str(), p->bundle == b->id)) {
                        p->bundle = b->id;
                        auto layout = ctx.ws.layout_of(b->id);
                        if (layout) p->width = std::max(layout.width, 1);
                        ctx.modified();
                    }
                    if (!b->description.empty()) SetItemTooltip("%s", b->description.c_str());
                    PopID();
                }
            }
            EndCombo();
        }
        SetItemTooltip("plain bits, or a bundle of named fields");

        if (p->bundle != model::no_id) {
            if (Checkbox("Flipped", &p->flipped)) ctx.modified();
            SetItemTooltip("the other view of the bundle, its inputs and outputs swapped");
            TextDisabled("%d bits", p->width);
            bundle_fields_table(ctx.ws.layout_of(p->bundle, p->flipped));
        } else {
            if (InputInt("Width", &p->width)) {
                p->width = glm::clamp(p->width, 1, model::max_width);
                ctx.modified();
            }
            if (p->width > 1 && edit_number_format("Format", p->format, p->width)) ctx.modified();
            SetItemTooltip("how its value is shown and entered while simulating");
        }

        TextUnformatted("Description");
        if (input_multiline("##description", p->description, {-FLT_MIN, GetTextLineHeight() * 4})) ctx.modified();
        SetItemTooltip("shown when hovering the pin of an instance of this cell");
        return;
    }

    if (elements.size() > 1) {
        selection_properties(elements);
        return;
    }

    SeparatorText("Cell");
    Text("%s / %s", ctx.lib.name.c_str(), ctx.cell.name.c_str());
    if (!ctx.lib.read_only && edit_category(ctx.lib, ctx.cell.category)) ctx.modified();
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
            if (p.bundle != model::no_id)
                Text("%s (%d)", ctx.ws.bundle_label(p.bundle, p.flipped).c_str(), p.width);
            else
                Text("%d", p.width);
        }
        EndTable();
    }

    for (auto& problem : ctx.cell.check())
        TextColored(ui::color(ui::color_id::warning), "%s", problem.c_str());
    for (auto& problem : nets.problems)
        TextColored(ui::color(ui::color_id::warning), "%s", problem.c_str());
}

}
