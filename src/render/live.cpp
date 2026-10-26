#include "live.h"
#include "../builtin/builtin.h"

#include <algorithm>
#include <cstdio>
#include <functional>

namespace logic::render {

namespace {

glm::vec4 color_of(const std::string& name)
{
    auto& c = colors();
    if (name == "green") return c.display_green;
    if (name == "yellow") return c.display_yellow;
    if (name == "orange") return c.display_orange;
    if (name == "blue") return c.display_blue;
    if (name == "cyan") return c.display_cyan;
    if (name == "white") return c.display_white;
    return c.display_red;
}

glm::vec4 dim(glm::vec4 c)
{
    return {c.r * 0.25f, c.g * 0.25f, c.b * 0.25f, 0.8f};
}

// 1, 0 or -1 for unknown, inverted if active low
int level(const sim::value& v, int bit, bool active_low)
{
    auto b = v.get(bit);
    if (b != sim::bit_state::zero && b != sim::bit_state::one) return -1;
    return (b == sim::bit_state::one) != active_low ? 1 : 0;
}

// thickness of the segments
constexpr float seg7_thickness = 5.0f;

// the hexagons of segments a to g of a slanted digit, in symbol
// coordinates. matches the box of seg7_display in builtin/io.cpp.
const std::vector<std::vector<glm::vec2>>& seg7_polygons()
{
    static const auto polys = [] {
        // the outer edges of the digit
        const float x0 = -8, x1 = 19, y0 = -23, y1 = 23, t = seg7_thickness, gap = 0.9f, slant = 0.1f;
        float l = x0 + t / 2, r = x1 - t / 2, b = y0 + t / 2, top = y1 - t / 2;
        const glm::vec2 ends[7][2] = {
            {{l, top}, {r, top}}, // a
            {{r, top}, {r, 0}}, // b
            {{r, 0}, {r, b}}, // c
            {{l, b}, {r, b}}, // d
            {{l, 0}, {l, b}}, // e
            {{l, top}, {l, 0}}, // f
            {{l, 0}, {r, 0}}, // g
        };
        std::vector<std::vector<glm::vec2>> out;
        for (auto& e : ends) {
            auto u = glm::normalize(e[1] - e[0]);
            glm::vec2 n{-u.y, u.x};
            auto p = e[0] + u * gap, q = e[1] - u * gap;
            float h = t / 2;
            std::vector<glm::vec2> poly{p, p + (u + n) * h, q + (n - u) * h, q, q - (u + n) * h, p + (u - n) * h};
            for (auto& v : poly)
                v.x += slant * v.y;
            out.push_back(std::move(poly));
        }
        return out;
    }();
    return polys;
}

void draw_seg7(renderer& r, const model::param_values& params, const std::vector<model::port>& ports, const std::vector<sim::value>& pins,
    const bool* shown, const std::function<glm::vec2(glm::vec2)>& world)
{
    auto& c = colors();
    auto it = params.find("color");
    auto lit = color_of(it != params.end() && std::get_if<std::string>(&it->second) ? std::get<std::string>(it->second) : std::string());
    auto unlit = c.display_unlit;

    int levels[8];
    builtin::seg7_levels(params, ports, pins, levels);
    auto color_at = [&](int k) {
        if (levels[k] < 0) return c.unknown;
        return (shown ? shown[k] : levels[k] == 1) ? lit : unlit;
    };

    for (int k = 0; k < 7; k++) {
        std::vector<glm::vec2> poly;
        for (auto p : seg7_polygons()[k])
            poly.push_back(world(p));
        auto color = color_at(k);
        r.fill(poly, color);
        // fills have hard edges, a framebuffer pixel wide line along them
        // smooths them
        for (size_t i = 0; i < poly.size(); i++)
            r.line(poly[i], poly[(i + 1) % poly.size()], color, r.g.pixel());
    }
    auto dp = params.find("dp");
    if (dp != params.end() && std::get_if<bool>(&dp->second) && std::get<bool>(dp->second)) r.circle(world({24, -21}), 3.0f, color_at(7));
}

}

void draw_live(renderer& r, const std::string& kind, const model::param_values& params, const model::instance& inst, const model::symbol& sym,
    const std::vector<model::port>& ports, const std::vector<sim::value>& pins, const bool* shown)
{
    auto world = [&](glm::vec2 p) { return glm::vec2(inst.pos) + inst.xf.apply(p); };
    // also while editing, with the segments dark
    if (kind == "seg7_display") return draw_seg7(r, params, ports, pins, shown, world);
    if (pins.empty()) return;
    auto get_bool = [&](const char* name) {
        auto it = params.find(name);
        return it != params.end() && std::get_if<bool>(&it->second) && std::get<bool>(it->second);
    };
    auto get_text = [&](const char* name) {
        auto it = params.find(name);
        return it != params.end() && std::get_if<std::string>(&it->second) ? std::get<std::string>(it->second) : std::string();
    };
    auto& c = colors();

    if (kind == "led") {
        auto on = level(pins[0], 0, get_bool("active_low"));
        auto color = color_of(get_text("color"));
        r.circle(world({0, 0}), 5.5f, on == 1 ? color : on == 0 ? dim(color) : c.unknown);
    } else if (kind == "hex_display") {
        auto b = sym.bounds();
        auto& v = pins[0];
        std::string text = v.fully_known() ? sim::to_hex(v) : v.fully_floating() ? "z" : "x";
        r.text_middle(text, world((b.min + b.max) / 2.0f), 8.0f, value_color(v), align::center);
    } else if (kind == "button" || kind == "switch") {
        // the output is what the user set
        auto on = level(pins[0], 0, get_bool("active_low"));
        if (on == 1) {
            auto b = sym.bounds();
            auto lo = world(glm::vec2(-8, -8)), hi = world(glm::vec2(8, 8));
            (void)b;
            r.rect(glm::min(lo, hi), glm::max(lo, hi), c.high, 2.0f);
        }
        if (kind == "switch") {
            // the lever up or down
            float y = on == 1 ? 5.0f : -5.0f;
            r.line(world({-5, y}), world({5, y}), c.symbol, 2.0f);
        }
    }
}

}
