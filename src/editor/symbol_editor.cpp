#include "symbol_editor.h"

#include <algorithm>
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
    } else if (auto a = std::get_if<model::arc>(&s.geom)) {
        float d = 1e30f;
        auto pts = model::arc_points(*a);
        for (size_t k = 1; k < pts.size(); k++)
            d = std::min(d, distance_to_segment(p, pts[k - 1], pts[k]));
        return d;
    } else if (auto c = std::get_if<model::circle>(&s.geom)) {
        return std::abs(glm::length(p - glm::vec2(c->center)) - c->radius);
    } else if (auto t = std::get_if<model::text>(&s.geom)) {
        // rough box from the anchor, the model has no font metrics
        glm::vec2 size{t->str.size() * t->size * 0.6f, t->size};
        auto rel = p - glm::vec2(t->pos);
        if (t->align == model::text_align::right) rel.x += size.x;
        if (t->align == model::text_align::center) rel.x += size.x / 2;
        return (rel.x >= 0 && rel.y >= 0 && rel.x <= size.x && rel.y <= size.y) ? 0.0f : 1e30f;
    }
    return 1e30f;
}

glm::ivec2 round(glm::vec2 p) { return glm::ivec2(glm::round(p)); }

// moves all points, a circle keeps its radius
void translate(model::shape& s, glm::ivec2 d)
{
    int n = std::holds_alternative<model::circle>(s.geom) ? 1 : model::point_count(s);
    for (int i = 0; i < n; i++)
        model::set_point(s, i, model::get_point(s, i) + d);
}

// copied shapes following the mouse until a click places them
class paste_tool : public tool<symbol_editor> {
public:
    explicit paste_tool(model::symbol part)
        : part(std::move(part))
    {
    }

    void update(symbol_editor& e, bool hovered) override
    {
        using namespace ImGui;

        offset = model::snap(e.view.mouse_world(), e.drag_step());
        if (!hovered) return;

        if (IsKeyPressed(ImGuiKey_Escape)) {
            done = true;
        } else if (IsMouseClicked(ImGuiMouseButton_Left)) {
            e.insert(part, offset);
            done = !GetIO().KeyShift;
        }
    }

    void draw(symbol_editor& e) override
    {
        for (auto s : part.shapes) {
            translate(s, offset);
            render::draw_shape(e.ctx.r, s, {0, 0}, {}, render::colors().preview);
        }
    }

private:
    model::symbol part;
    glm::ivec2 offset{0, 0};
};

// places a pin linked to the first port without a pin, shift keeps placing
class pin_tool : public tool<symbol_editor> {
public:
    void update(symbol_editor& e, bool hovered) override
    {
        using namespace ImGui;

        preview.pos = model::snap(e.view.mouse_world(), model::grid);
        preview.port = model::no_id;
        for (auto& p : e.ctx.cell.ports) {
            if (!e.sym.find_pin_of_port(p.id)) {
                preview.port = p.id;
                break;
            }
        }
        if (!hovered) return;

        if (IsKeyPressed(ImGuiKey_Escape)) {
            done = true;
        } else if (IsMouseClicked(ImGuiMouseButton_Left)) {
            auto p = preview;
            p.id = e.ctx.cell.new_id();
            e.sym.pins.push_back(p);
            e.selection = {{symbol_editor::handle::pin, p.id}};
            e.ctx.modified();
            done = !GetIO().KeyShift;
        }
    }

    void draw(symbol_editor& e) override { e.draw_pin(preview, render::colors().preview); }

private:
    model::pin preview;
};

// draws a shape by clicking its points in order, shift keeps drawing
class shape_tool : public tool<symbol_editor> {
public:
    explicit shape_tool(model::shape s)
        : proto(std::move(s))
        , draft(proto)
    {
    }

    void update(symbol_editor& e, bool hovered) override
    {
        using namespace ImGui;

        // points not placed yet follow the mouse
        for (int i = step; i < model::point_count(draft); i++)
            model::set_point(draft, i, round(e.view.mouse_world()));
        if (!hovered) return;

        if (IsKeyPressed(ImGuiKey_Escape)) {
            // drops a partly drawn shape, not busy any more so the tool can end
            step = 0;
            done = true;
        } else if (IsMouseClicked(ImGuiMouseButton_Left)) {
            step++;
            if (step == model::point_count(draft)) {
                draft.id = e.ctx.cell.new_id();
                e.sym.shapes.push_back(draft);
                e.ctx.modified();
                done = !GetIO().KeyShift;
                draft = proto;
                step = 0;
            }
        }
    }

    void draw(symbol_editor& e) override
    {
        auto& r = e.ctx.r;
        render::draw_shape(r, draft, {0, 0}, {}, render::colors().preview);
        for (int i = 0; i < model::point_count(draft); i++)
            r.circle(model::get_point(draft, i), 1.0f, render::colors().control_point);
    }

    // a partly drawn shape is not part of the symbol yet
    bool busy() const override { return step > 0; }

private:
    model::shape proto;
    model::shape draft;
    int step = 0;
};

}

symbol_editor::symbol_editor(context c)
    : ctx(c)
    , sym(c.cell.symbol)
    , active(std::make_unique<select_tool<symbol_editor>>())
{
}

void symbol_editor::set_tool(tool_kind k)
{
    if (busy()) return;

    std::unique_ptr<tool<symbol_editor>> t;
    switch (k) {
    case tool_kind::select: t = std::make_unique<select_tool<symbol_editor>>(); break;
    case tool_kind::pin: t = std::make_unique<pin_tool>(); break;
    case tool_kind::line: t = std::make_unique<shape_tool>(model::shape{model::no_id, model::line{}}); break;
    case tool_kind::bezier: t = std::make_unique<shape_tool>(model::shape{model::no_id, model::bezier{}}); break;
    case tool_kind::arc: t = std::make_unique<shape_tool>(model::shape{model::no_id, model::arc{}}); break;
    case tool_kind::circle: t = std::make_unique<shape_tool>(model::shape{model::no_id, model::circle{{0, 0}, 0.0f}}); break;
    case tool_kind::text: t = std::make_unique<shape_tool>(model::shape{model::no_id, model::text{}}); break;
    // needs something copied, see begin_paste()
    case tool_kind::paste: return;
    }

    if (k != tool_kind::select) selection.clear();
    kind = k;
    active = std::move(t);
}

std::optional<model::cell> symbol_editor::copy_selection() const
{
    auto bounds = selection_bounds();
    if (!bounds) return std::nullopt;
    auto origin = model::snap((bounds->min + bounds->max) / 2.0f, model::grid);

    model::cell part;
    part.name = ctx.cell.name;
    for (auto s : sym.shapes) {
        if (!is_shape_selected(s.id)) continue;
        translate(s, -origin);
        part.symbol.shapes.push_back(std::move(s));
    }
    if (part.symbol.shapes.empty()) return std::nullopt;
    return part;
}

void symbol_editor::begin_paste(model::cell part)
{
    if (busy() || part.symbol.shapes.empty()) return;
    selection.clear();
    kind = tool_kind::paste;
    active = std::make_unique<paste_tool>(std::move(part.symbol));
}

void symbol_editor::insert(const model::symbol& part, glm::ivec2 offset)
{
    selection.clear();
    for (auto s : part.shapes) {
        s.id = ctx.cell.new_id();
        translate(s, offset);
        for (int i = 0; i < model::point_count(s); i++)
            selection.push_back({handle::point, s.id, i});
        sym.shapes.push_back(std::move(s));
    }
    ctx.modified();
}

void symbol_editor::process_inputs(bool hovered)
{
    using namespace ImGui;

    if (hovered) view.handle_view_input();

    // shortcuts with ctrl belong to the app (undo, save)
    if (hovered && !busy() && !GetIO().KeyCtrl) {
        if (IsKeyPressed(ImGuiKey_G)) ctx.r.show_grid = !ctx.r.show_grid;

        if (IsKeyPressed(ImGuiKey_P))
            set_tool(tool_kind::pin);
        else if (IsKeyPressed(ImGuiKey_L))
            set_tool(tool_kind::line);
        else if (IsKeyPressed(ImGuiKey_B))
            set_tool(tool_kind::bezier);
        else if (IsKeyPressed(ImGuiKey_A))
            set_tool(tool_kind::arc);
        else if (IsKeyPressed(ImGuiKey_C))
            set_tool(tool_kind::circle);
        else if (IsKeyPressed(ImGuiKey_T))
            set_tool(tool_kind::text);
    }

    int turns;
    bool mirror;
    if (hovered && !busy() && kind == tool_kind::select && transform_shortcut(turns, mirror)) transform_selection(turns, mirror);

    active->update(*this, hovered);
    if (active->done) set_tool(tool_kind::select);
}

int symbol_editor::drag_step() const
{
    bool has_pin = std::any_of(selection.begin(), selection.end(), [](auto& h) { return h.kind == handle::pin; });
    return has_pin ? model::grid : 1;
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

void symbol_editor::transform_selection(int turns, bool mirror)
{
    auto b = selection_bounds();
    if (!b) return;
    auto pivot = pivot_of(*b);
    for (auto& h : selection) {
        if (h.kind == handle::pin) {
            if (auto p = sym.find_pin(h.id)) p->pos = turn_about(p->pos, pivot, turns, mirror);
            continue;
        }
        auto s = sym.find_shape(h.id);
        if (!s) continue;
        // a circle turns with its center, the radius stays
        if (std::holds_alternative<model::circle>(s->geom) && h.index == 1) continue;
        model::set_point(*s, h.index, turn_about(model::get_point(*s, h.index), pivot, turns, mirror));
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
    auto& c = render::colors();
    r.begin(view.vp());

    // symbols might be replaced from the properties panel
    validate_selection();

    // origin of the symbol, the instance position in schematics
    float hair = r.pixel();
    r.line({-5, 0}, {5, 0}, c.preview, hair);
    r.line({0, -5}, {0, 5}, c.preview, hair);
    r.ring({0, 0}, 2.5f, c.preview, hair);

    for (auto& s : sym.shapes) {
        auto color = is_shape_selected(s.id) ? c.selection : c.symbol;
        render::draw_shape(r, s, {0, 0}, {}, color);

        for (int i = 0; i < model::point_count(s); i++) {
            bool sel = is_selected({handle::point, s.id, i});
            r.circle(model::get_point(s, i), sel ? 1.5f : 1.0f, sel ? c.selection : c.control_point);
        }
    }

    for (auto& p : sym.pins)
        draw_pin(p, is_selected({handle::pin, p.id}) ? c.selection : c.pin);

    active->draw(*this);
    r.end();
}

void symbol_editor::draw_pin(const model::pin& p, glm::vec4 color)
{
    auto& r = ctx.r;
    auto& c = render::colors();
    auto port = ctx.cell.find_port(p.port);
    glm::vec2 pos = p.pos;
    r.rect_outline(pos - 2.5f, pos + 2.5f, port ? color : c.error, 0.8f, 0.8f);
    r.text(port ? port->name : "?", pos + glm::vec2{0, 4}, 6.0f, port ? c.pin : c.error, render::align::center);
}

void symbol_editor::selection_properties()
{
    using namespace ImGui;

    // the selected shapes once each, split in strokes and texts
    std::vector<model::shape*> strokes, texts;
    std::set<model::id> seen;
    size_t pins = 0;
    for (auto& h : selection) {
        if (h.kind == handle::pin) {
            pins++;
            continue;
        }
        if (!seen.insert(h.id).second) continue;
        if (auto s = sym.find_shape(h.id)) (std::holds_alternative<model::text>(s->geom) ? texts : strokes).push_back(s);
    }

    SeparatorText("Selection");
    Text("%zu shapes, %zu pins", strokes.size() + texts.size(), pins);
    if (!strokes.empty() || !texts.empty()) TextDisabled("changes apply to all of them");

    if (!strokes.empty()) {
        bool same = std::all_of(strokes.begin(), strokes.end(), [&](auto s) { return s->width == strokes[0]->width; });
        float w = strokes[0]->width;
        if (DragFloat("Width", &w, 0.05f, 0.0f, 10.0f, !same ? "mixed" : w == 0.0f ? "normal" : "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
            for (auto s : strokes)
                s->width = w;
            ctx.modified();
        }
        SetItemTooltip("stroke width of the lines, beziers, arcs and circles, 0 is the normal width. %.1f is as thick as a bus.", model::bus_stroke);
    }

    if (!texts.empty()) {
        auto size_of = [](model::shape* s) { return std::get<model::text>(s->geom).size; };
        bool same = std::all_of(texts.begin(), texts.end(), [&](auto s) { return size_of(s) == size_of(texts[0]); });
        float size = size_of(texts[0]);
        if (SliderFloat("Text size", &size, 1.0f, 40.0f, same ? "%.1f" : "mixed")) {
            for (auto s : texts)
                std::get<model::text>(s->geom).size = size;
            ctx.modified();
        }
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
            int align = (int)t->align;
            if (Combo("Align", &align, "left\0right\0center\0")) {
                t->align = (model::text_align)align;
                ctx.modified();
            }
            SetItemTooltip("which end of the text its point is");
        } else if (auto c = std::get_if<model::circle>(&s->geom)) {
            SeparatorText("Circle");
            if (InputInt2("Center", &c->center.x)) ctx.modified();
            if (InputFloat("Radius", &c->radius, 0.5f, 5.0f)) {
                c->radius = std::max(c->radius, 0.0f);
                ctx.modified();
            }
        } else {
            bool is_arc = std::holds_alternative<model::arc>(s->geom);
            SeparatorText(std::holds_alternative<model::line>(s->geom) ? "Line" : (is_arc ? "Arc" : "Bezier"));
            for (int i = 0; i < model::point_count(*s); i++) {
                auto p = model::get_point(*s, i);
                PushID(i);
                if (InputInt2(i == 2 ? (is_arc ? "Through" : "Control") : (i == 0 ? "Start" : "End"), &p.x)) {
                    model::set_point(*s, i, p);
                    ctx.modified();
                }
                PopID();
            }
        }
        if (!std::holds_alternative<model::text>(s->geom)) {
            if (DragFloat("Width", &s->width, 0.05f, 0.0f, 10.0f, s->width == 0.0f ? "normal" : "%.2f", ImGuiSliderFlags_AlwaysClamp)) ctx.modified();
            SetItemTooltip("stroke width, 0 is the normal width. %.1f is as thick as a bus.", model::bus_stroke);
        }
        return;
    }

    if (!selection.empty()) {
        selection_properties();
        return;
    }

    SeparatorText("Symbol");
    Text("%zu shapes, %zu pins", sym.shapes.size(), sym.pins.size());

    if (Button("Generate from ports")) {
        if (sym.shapes.empty() && sym.pins.empty()) {
            ctx.cell.generate_symbol(ctx.cell.is_external() ? model::pin_order::declaration : model::pin_order::position);
            ctx.modified();
        } else {
            OpenPopup("Replace symbol?");
        }
    }
    SetItemTooltip("Replace the symbol with a box, inputs on the left and outputs on the right");

    if (BeginPopupModal("Replace symbol?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        TextUnformatted("This replaces all shapes and pins of the symbol.");
        if (Button("Replace")) {
            ctx.cell.generate_symbol(ctx.cell.is_external() ? model::pin_order::declaration : model::pin_order::position);
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
