#include "renderer.h"

#include <Hd/Resource.h>
#include <glm/gtc/type_ptr.hpp>

namespace logic::render {

renderer::renderer()
    : grid_dot_shader(Hd::Resource::getExeRes("res/shaders/grid_dot_vert.glsl").first, Hd::Resource::getExeRes("res/shaders/grid_dot_frag.glsl").first)
{
}

void renderer::grid(const glm::mat4& vp, float zoom)
{
    float c = glm::smoothstep(1.0f / 1000, 1.0f / 300, zoom) * 0.3f;
    glm::vec4 color = {c, c, c, 1.0f};

    grid_dot_shader.Bind();
    glUniformMatrix4fv(0, 1, false, glm::value_ptr(vp));
    glUniform4fv(1, 1, &color.x);

    grid_vao.Bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void renderer::text(const glm::mat4& vp, const std::string& str, glm::vec2 pos, float size, glm::vec4 color, align a)
{
    if (str.empty()) return;

    g.setColor(color);
    g.setText(str);

    if (a != align::left) {
        auto box = g.getTextBox();
        float w = (box.second.x - box.first.x) * size;
        pos.x -= (a == align::center) ? w / 2.0f : w;
    }

    g.text(vp, pos, size);
}

void draw_shape(renderer& r, const glm::mat4& vp, const model::shape& s, glm::ivec2 pos, model::transform xf, glm::vec4 color)
{
    auto& g = r.g;
    g.setColor(color);

    auto tp = [&](glm::vec2 p) { return glm::vec2(pos) + xf.apply(p); };

    if (auto l = std::get_if<model::line>(&s.geom)) {
        g.line(vp, tp(l->a), tp(l->b));
    } else if (auto b = std::get_if<model::bezier>(&s.geom)) {
        constexpr int n = 16;
        glm::vec2 a = b->a, c = b->c, e = b->b;
        glm::vec2 prev = a;
        for (int k = 1; k <= n; k++) {
            float t = float(k) / n;
            glm::vec2 p = (1 - t) * (1 - t) * a + 2 * (1 - t) * t * c + t * t * e;
            g.line(vp, tp(prev), tp(p));
            prev = p;
        }
    } else if (auto c = std::get_if<model::circle>(&s.geom)) {
        g.circle(vp, tp(c->center), c->radius);
    } else if (auto t = std::get_if<model::text>(&s.geom)) {
        // text is not rotated, only its anchor moves with the transform
        r.text(vp, t->str, tp(t->pos), t->size, color);
    }
}

void draw_symbol(renderer& r, const glm::mat4& vp, const model::symbol& s, glm::ivec2 pos, model::transform xf, glm::vec4 color)
{
    for (auto& sh : s.shapes)
        draw_shape(r, vp, sh, pos, xf, color);

    r.g.setColor(colors::pin);
    for (auto& p : s.pins)
        r.g.circle(vp, glm::vec2(pos) + xf.apply(glm::vec2(p.pos)), 1.5f, true);
}

void draw_instance(renderer& r, const glm::mat4& vp, const model::workspace& ws, const model::instance& i, glm::vec4 color)
{
    auto iface = ws.interface_of(i);

    if (!iface) {
        auto b = model::instance_bounds(i, nullptr);
        r.g.setColor(colors::error);
        r.g.rect(vp, b.min, b.max);
        r.text(vp, "?", glm::vec2(i.pos) - glm::vec2{0, 3}, 10.0f, colors::error, align::center);
        return;
    }

    draw_symbol(r, vp, *iface.symbol, i.pos, i.xf, color);

    if (!i.name.empty()) {
        auto b = model::instance_bounds(i, iface.symbol);
        r.text(vp, i.name, {b.min.x, b.max.y + 3}, 6.0f, colors::preview);
    }
}

void draw_port(renderer& r, const glm::mat4& vp, const model::port& p, glm::vec4 color)
{
    auto& g = r.g;
    glm::vec2 pos = p.pos;
    glm::vec2 cp = model::connection_point(p);

    g.setColor(color);
    if (p.dir == model::direction::input) {
        g.rect(vp, pos - glm::vec2{5, 5}, pos + glm::vec2{5, 5});
        g.line(vp, pos + glm::vec2{5, 0}, cp);
    } else {
        g.circle(vp, pos, 5.0f);
        g.line(vp, pos - glm::vec2{5, 0}, cp);
    }

    if (p.dir == model::direction::inout) {
        g.rect(vp, pos - glm::vec2{5, 5}, pos + glm::vec2{5, 5});
    }

    std::string label = p.width > 1 ? p.name + "[" + std::to_string(p.width) + "]" : p.name;
    if (p.dir == model::direction::input)
        r.text(vp, label, pos + glm::vec2{-8, -3}, 10.0f, color, align::right);
    else
        r.text(vp, label, pos + glm::vec2{8, -3}, 10.0f, color, align::left);
}

void draw_wire(renderer& r, const glm::mat4& vp, const model::wire& w, glm::vec4 color, bool bus)
{
    r.g.setColor(color);
    if (!bus) {
        r.g.line(vp, w.a, w.b);
    } else if (w.a.x == w.b.x || w.a.y == w.b.y) {
        r.g.rect(vp, glm::min(w.a, w.b) - 1, glm::max(w.a, w.b) + 1, true);
    } else {
        // lines have no width, a few parallel ones
        auto n = glm::normalize(glm::vec2(w.b - w.a));
        n = {-n.y, n.x};
        for (float d : {-0.8f, -0.4f, 0.0f, 0.4f, 0.8f})
            r.g.line(vp, glm::vec2(w.a) + d * n, glm::vec2(w.b) + d * n);
    }
    r.g.rect(vp, glm::vec2(w.a) - 0.75f, glm::vec2(w.a) + 0.75f, true);
    r.g.rect(vp, glm::vec2(w.b) - 0.75f, glm::vec2(w.b) + 0.75f, true);
}

glm::vec4 value_color(const sim::value& v, int width)
{
    if (width == 0) return colors::floating;
    auto m = sim::mask(width);
    if ((v.z & m) == m) return colors::floating;
    if ((v.x | v.z) & m) return colors::unknown;
    if (width > 1) return colors::bus;
    return (v.bits & 1) ? colors::high : colors::low;
}

void draw_port_value(renderer& r, const glm::mat4& vp, const model::port& p, const sim::value& v)
{
    auto color = value_color(v, p.width);
    auto str = sim::to_string(v, p.width);
    glm::vec2 pos = p.pos;
    if (p.width == 1)
        r.text(vp, str, pos - glm::vec2{0, 3}, 8.0f, color, align::center);
    else
        r.text(vp, str, pos + glm::vec2{0, 8}, 7.0f, color, align::center);
}

}
