#include "renderer.h"
#include <cstddef>
#include <algorithm>
#include "../sim/numbers.h"

#include <Hd/Resource.h>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>

namespace logic::render {

namespace {

constexpr glm::vec4 rgb(int hex, float a = 1.0f)
{
    return {((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f, a};
}

const theme* current = &dark_theme();

}

const theme& dark_theme()
{
    static const theme t{
        .background = rgb(0x15171a),
        .grid = rgb(0x6a7080, 0.8f),
        .symbol = rgb(0xe4e7eb),
        .wire = rgb(0xa2a9b3),
        .bus_wire = rgb(0x6fa8dc),
        .bundle_wire = rgb(0xb98ae6),
        .label = rgb(0x8a919c),
        .selection = rgb(0xffc533),
        .guide = rgb(0x6f8a33),
        .pin = rgb(0x5a9bff),
        .control_point = rgb(0xff4d4d),
        .error = rgb(0xff4d4d),
        .width_error = rgb(0xff8c1a),
        .preview = rgb(0x8a919c),
        .high = rgb(0x3ee65a),
        .low = rgb(0x1d7a2f),
        .unknown = rgb(0xff4d4d),
        .floating = rgb(0x5a8cff),
        .bus = rgb(0x8cc8f2),
        .bundle = rgb(0xd2b0f5),
        .symbol_fill = rgb(0x1f2b38),
    };
    return t;
}

const theme& light_theme()
{
    static const theme t{
        .background = rgb(0xfafaf8),
        .grid = rgb(0x9aa0ab, 0.8f),
        .symbol = rgb(0x1e2126),
        .wire = rgb(0x4d545e),
        .bus_wire = rgb(0x2f6fb5),
        .bundle_wire = rgb(0x8a3fc4),
        .label = rgb(0x737a85),
        .selection = rgb(0xf08c00),
        .guide = rgb(0x8ca640),
        .pin = rgb(0x1a66e6),
        .control_point = rgb(0xd91a1a),
        .error = rgb(0xd91a1a),
        .width_error = rgb(0xf07800),
        .preview = rgb(0x9097a3),
        .high = rgb(0x12b333),
        .low = rgb(0x0d6120),
        .unknown = rgb(0xe01b1b),
        .floating = rgb(0x2966f0),
        .bus = rgb(0x1f7acc),
        .bundle = rgb(0x9b4fd9),
        .symbol_fill = rgb(0xdce8f5),
    };
    return t;
}

const theme& colors()
{
    return *current;
}

void set_theme(const theme& t)
{
    current = &t;
}

renderer::renderer()
    : g((const unsigned char*)Hd::Resource::getExeRes("res/fonts/Roboto.ttf").first)
    , grid_shader(Hd::Resource::getExeRes("res/shaders/grid_vert.glsl").first, Hd::Resource::getExeRes("res/shaders/grid_frag.glsl").first)
    , fill_shader(Hd::Resource::getExeRes("res/shaders/fill_vert.glsl").first, Hd::Resource::getExeRes("res/shaders/fill_frag.glsl").first)
{
    glGenBuffers(1, &fill_vbo);
    fill_vao.Bind();
    glBindBuffer(GL_ARRAY_BUFFER, fill_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(fill_vertex), (void*)offsetof(fill_vertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(fill_vertex), (void*)offsetof(fill_vertex, color));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

renderer::~renderer()
{
    glDeleteBuffers(1, &fill_vbo);
}

void renderer::begin(const glm::mat4& vp)
{
    auto& bg = colors().background;
    glClearColor(bg.r, bg.g, bg.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    g.begin(vp);
    frame_vp = vp;
    if (show_grid) draw_grid(vp);
}

void renderer::end()
{
    // under the lines and text
    draw_fills(frame_vp);
    g.end();
}

void renderer::draw_origin()
{
    if (!show_origin) return;
    auto c = colors().preview;
    float hair = pixel();
    line({-5, 0}, {5, 0}, c, hair);
    line({0, -5}, {0, 5}, c, hair);
    ring({0, 0}, 2.5f, c, hair);
}

namespace {

// triangles of a simple polygon by ear clipping, any winding. a polygon
// that isn't simple keeps the triangles found until no ear is left.
std::vector<glm::vec2> triangulate(std::vector<glm::vec2> ps)
{
    std::vector<glm::vec2> r;
    // counter clockwise
    float area = 0;
    for (size_t k = 0; k < ps.size(); k++)
        area += ps[k].x * ps[(k + 1) % ps.size()].y - ps[(k + 1) % ps.size()].x * ps[k].y;
    if (area < 0) std::reverse(ps.begin(), ps.end());

    auto cross = [](glm::vec2 a, glm::vec2 b, glm::vec2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
    std::vector<int> idx(ps.size());
    for (size_t k = 0; k < ps.size(); k++)
        idx[k] = (int)k;

    while (idx.size() > 3) {
        int n = (int)idx.size();
        bool clipped = false;
        for (int k = 0; k < n && !clipped; k++) {
            auto a = ps[idx[(k + n - 1) % n]], b = ps[idx[k]], c = ps[idx[(k + 1) % n]];
            float turn = cross(a, b, c);
            // collinear points go without a triangle
            if (std::abs(turn) < 1e-6f) {
                idx.erase(idx.begin() + k);
                clipped = true;
                break;
            }
            if (turn < 0) continue;
            bool empty = true;
            for (int j = 0; j < n && empty; j++) {
                if (j == k || j == (k + 1) % n || j == (k + n - 1) % n) continue;
                auto p = ps[idx[j]];
                if (cross(a, b, p) > 0 && cross(b, c, p) > 0 && cross(c, a, p) > 0) empty = false;
            }
            if (!empty) continue;
            r.insert(r.end(), {a, b, c});
            idx.erase(idx.begin() + k);
            clipped = true;
        }
        if (!clipped) return r;
    }
    if (idx.size() == 3) r.insert(r.end(), {ps[idx[0]], ps[idx[1]], ps[idx[2]]});
    return r;
}

}

void renderer::fill(const std::vector<glm::vec2>& polygon, glm::vec4 color)
{
    for (auto p : triangulate(polygon))
        fills.push_back({p, color});
}

void renderer::fill_symbol(const model::symbol& s, glm::ivec2 pos, model::transform xf, glm::vec4 color)
{
    if (color.a <= 0.0f) return;
    auto it = fill_cache.find(&s);
    if (it == fill_cache.end() || it->second.symbol != s) {
        // symbols that are gone leave stale entries, start over now and then
        if (fill_cache.size() > 2000) fill_cache.clear();
        fill_entry e{s, {}};
        for (auto& outline : model::fill_outlines(s)) {
            auto t = triangulate(outline);
            e.triangles.insert(e.triangles.end(), t.begin(), t.end());
        }
        it = fill_cache.insert_or_assign(&s, std::move(e)).first;
    }
    for (auto p : it->second.triangles)
        fills.push_back({glm::vec2(pos) + xf.apply(p), color});
}

void renderer::draw_fills(const glm::mat4& vp)
{
    if (fills.empty()) return;
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    fill_shader.Bind();
    glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(vp));
    fill_vao.Bind();
    glBindBuffer(GL_ARRAY_BUFFER, fill_vbo);
    // the buffer only grows, rewriting it in place is cheap for drivers
    // that are slow to respecify buffers many times per frame (icons)
    size_t bytes = fills.size() * sizeof(fill_vertex);
    if (bytes > fill_capacity) {
        fill_capacity = std::max(bytes, 2 * fill_capacity);
        glBufferData(GL_ARRAY_BUFFER, fill_capacity, nullptr, GL_DYNAMIC_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, fills.data());
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)fills.size());
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    fills.clear();
}

void renderer::draw_grid(const glm::mat4& vp)
{
    // zoomed out the circuit gets small and thin, the grid would cover it.
    // below 16 pixels per grid step the model grid fades out (gone at 5)
    // and every 5th step stays, the whole grid fades out from 14 to 2.
    float step_px = model::grid / pixel();
    float visible = glm::smoothstep(2.0f, 14.0f, step_px);
    if (visible <= 0.0f) return;
    float fine = glm::clamp((step_px - 5.0f) / (16.0f - 5.0f), 0.0f, 1.0f);
    float spacing = model::grid;
    auto color = colors().grid;
    color.a *= visible;

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    grid_shader.Bind();
    auto inv = glm::inverse(vp);
    glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(inv));
    glUniform4fv(1, 1, &color.x);
    glUniform1f(2, pixel());
    glUniform1f(3, spacing);
    glUniform1f(4, fine);
    // 0 dots, 1 lines, 2 checkers
    int mode = 0;
    if (grid == grid_style::lines) mode = 1;
    if (grid == grid_style::checkers) mode = 2;
    glUniform1i(5, mode);

    grid_vao.Bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

void renderer::text(const std::string& str, glm::vec2 pos, float size, glm::vec4 color, align a)
{
    auto ha = a == align::left ? Hd::Renderer2D::Align::left : a == align::center ? Hd::Renderer2D::Align::center : Hd::Renderer2D::Align::right;
    g.text(str, pos, size, color, ha);
}

void renderer::text_middle(const std::string& str, glm::vec2 pos, float size, glm::vec4 color, align a)
{
    text(str, {pos.x, pos.y - g.font().capHeight() * size / 2}, size, color, a);
}

namespace {

// marks a pin that isn't connected
void draw_pin(renderer& r, glm::vec2 pos, glm::vec4 color)
{
    r.circle(pos, 2.0f, color);
}

}

void draw_shape(renderer& r, const model::shape& s, glm::ivec2 pos, model::transform xf, glm::vec4 color)
{
    auto tp = [&](glm::vec2 p) { return glm::vec2(pos) + xf.apply(p); };
    // scaling up (icons) like thin lines would make buses and bundles blobs:
    // they grow by a quarter of their extra width over a thin line, and
    // never get thinner than they are
    float thin = stroke * r.stroke_scale;
    float width = s.width;
    // the stubs of bus and bundle pins are thin unless buses are thick
    if (!r.thick_buses && (width == model::bus_stroke || width == model::bundle_stroke)) width = 0.0f;
    float w = thin;
    if (width > 0.0f) w = r.stroke_scale > 1.0f ? std::max(width, thin * (1.0f + 0.25f * (width / stroke - 1.0f))) : width;

    if (auto l = std::get_if<model::line>(&s.geom)) {
        r.line(tp(l->a), tp(l->b), color, w);
    } else if (auto b = std::get_if<model::bezier>(&s.geom)) {
        r.g.bezier(tp(b->a), tp(b->c), tp(b->b), w, color);
    } else if (auto a = std::get_if<model::arc>(&s.geom)) {
        auto pts = model::arc_points(*a);
        for (size_t k = 1; k < pts.size(); k++)
            r.line(tp(pts[k - 1]), tp(pts[k]), color, w);
    } else if (auto c = std::get_if<model::circle>(&s.geom)) {
        r.ring(tp(c->center), c->radius, color, w);
    } else if (auto t = std::get_if<model::text>(&s.geom)) {
        // text is not rotated or mirrored. the box it covers moves with the
        // transform and the text is drawn in it, so a mirrored symbol keeps
        // its labels on the same side of its lines.
        float width = r.g.textWidth(t->str, t->size);
        glm::vec2 lo = t->pos;
        if (t->align == model::text_align::right) lo.x -= width;
        if (t->align == model::text_align::center) lo.x -= width / 2;
        glm::vec2 hi = lo + glm::vec2{width, r.g.font().capHeight() * t->size};
        auto a = tp(lo), b = tp(hi);
        r.text(t->str, glm::min(a, b), t->size, color);
    }
}

glm::vec4 fill_of(const renderer& r, const model::cell* c)
{
    if (c && c->fill) return *c->fill;
    return r.fill_symbols ? colors().symbol_fill : glm::vec4{};
}

void draw_symbol(renderer& r, const model::symbol& s, glm::ivec2 pos, model::transform xf, glm::vec4 color, const std::vector<bool>* pin_connected, glm::vec4 fill)
{
    r.fill_symbol(s, pos, xf, fill);
    for (auto& sh : s.shapes)
        draw_shape(r, sh, pos, xf, color);

    for (size_t k = 0; k < s.pins.size(); k++) {
        if (pin_connected && (*pin_connected)[k]) continue;
        draw_pin(r, glm::vec2(pos) + xf.apply(glm::vec2(s.pins[k].pos)), color);
    }
}

void draw_instance(renderer& r, const model::workspace& ws, const model::instance& i, glm::vec4 color, const std::vector<int>* pin_nets)
{
    auto iface = ws.interface_of(i);

    if (!iface) {
        auto b = model::instance_bounds(i, nullptr);
        r.rect_outline(b.min, b.max, colors().error, stroke, 2.0f);
        r.text("?", glm::vec2(i.pos) - glm::vec2{0, 3}, 10.0f, colors().error, align::center);
        return;
    }

    std::vector<bool> connected;
    if (pin_nets) {
        for (auto& pin : iface.symbol->pins) {
            bool c = false;
            for (size_t k = 0; k < iface.ports->size() && k < pin_nets->size(); k++)
                if ((*iface.ports)[k].id == pin.port) c = (*pin_nets)[k] >= 0;
            connected.push_back(c);
        }
    }
    draw_symbol(r, *iface.symbol, i.pos, i.xf, color, pin_nets ? &connected : nullptr, fill_of(r, ws.find_cell(i.cell)));

    bool show = r.names == instance_names::all || (r.names == instance_names::custom && !model::is_default_instance_name(i.name));
    if (!i.name.empty() && show) {
        auto b = model::instance_bounds(i, iface.symbol);
        r.text(i.name, {b.min.x, b.max.y + 2}, 6.0f, colors().label);
    }
}

void draw_port(renderer& r, const model::port& p, glm::vec4 color, bool connected, const std::string& type)
{
    glm::vec2 pos = p.pos;
    glm::vec2 cp = model::connection_point(p);

    if (p.dir == model::direction::input) {
        r.rect_outline(pos - glm::vec2{5, 5}, pos + glm::vec2{5, 5}, color, stroke, 1.5f);
        r.line(pos + glm::vec2{5, 0}, cp, color);
    } else {
        r.ring(pos, 5.0f, color);
        r.line(pos - glm::vec2{5, 0}, cp, color);
    }

    if (p.dir == model::direction::inout) r.rect_outline(pos - glm::vec2{5, 5}, pos + glm::vec2{5, 5}, color, stroke, 1.5f);
    if (!connected) draw_pin(r, cp, color);

    std::string label = !type.empty() ? p.name + " : " + type : p.width > 1 ? p.name + "[" + std::to_string(p.width) + "]"
                                                                            : p.name;
    if (p.dir == model::direction::input)
        r.text_middle(label, pos + glm::vec2{-8, 0}, 10.0f, color, align::right);
    else
        r.text_middle(label, pos + glm::vec2{8, 0}, 10.0f, color, align::left);
}

float renderer::wire_line(bool bus, bool bundle) const
{
    if (!thick_buses) return wire_width;
    return bundle ? bundle_width : bus ? bus_width
                                       : wire_width;
}

void draw_wire(renderer& r, const model::wire& w, glm::vec4 color, bool bus, bool bundle)
{
    r.line(w.a, w.b, color, r.wire_line(bus, bundle));
}

glm::vec4 value_color(const sim::value& v, bool bundle)
{
    auto& c = colors();
    if (v.fully_floating()) return c.floating;
    if (!v.fully_known()) return c.unknown;
    if (bundle) return c.bundle;
    if (v.width() > 1) return c.bus;
    return (v.to_u64() & 1) ? c.high : c.low;
}

void draw_port_value(renderer& r, const model::port& p, const sim::value& v)
{
    auto color = value_color(v, p.bundle != model::no_id);
    auto str = sim::format_number(v, p.format);
    glm::vec2 pos = p.pos;
    if (p.width == 1)
        r.text_middle(str, pos, 7.0f, color, align::center);
    else
        r.text(str, pos + glm::vec2{0, 8}, 7.0f, color, align::center);
}

}
