#include "live.h"

#include <cstdio>

namespace logic::render {

namespace {

glm::vec4 color_of(const std::string& name)
{
    if (name == "green") return {0.2f, 0.9f, 0.3f, 1};
    if (name == "yellow") return {1.0f, 0.85f, 0.1f, 1};
    if (name == "blue") return {0.3f, 0.55f, 1.0f, 1};
    if (name == "white") return {0.95f, 0.95f, 0.95f, 1};
    return {1.0f, 0.15f, 0.1f, 1};
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

}

void draw_live(renderer& r, const std::string& kind, const model::param_values& params, const model::instance& inst, const model::symbol& sym,
    const std::vector<sim::value>& pins)
{
    if (pins.empty()) return;
    auto world = [&](glm::vec2 p) { return glm::vec2(inst.pos) + inst.xf.apply(p); };
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
    } else if (kind == "seg7_display") {
        auto b = sym.bounds();
        // segments inside the box: y is up
        float x0 = b.min.x + 9, x1 = b.max.x - 9, y0 = b.min.y + 9, y1 = b.max.y - 9, ym = (y0 + y1) / 2;
        struct seg {
            glm::vec2 a, b;
        };
        const seg segs[7] = {
            {{x0, y1}, {x1, y1}}, // a
            {{x1, y1}, {x1, ym}}, // b
            {{x1, ym}, {x1, y0}}, // c
            {{x0, y0}, {x1, y0}}, // d
            {{x0, ym}, {x0, y0}}, // e
            {{x0, y1}, {x0, ym}}, // f
            {{x0, ym}, {x1, ym}}, // g
        };
        auto color = color_of(get_text("color"));
        bool low = get_bool("active_low");
        for (int k = 0; k < 7; k++) {
            auto on = level(pins[0], k, low);
            // a bit shorter, so the segments don't touch
            auto d = (segs[k].b - segs[k].a) * 0.12f;
            r.line(world(segs[k].a + d), world(segs[k].b - d), on == 1 ? color : on == 0 ? dim(color) : c.unknown, 3.0f);
        }
        if (pins[0].width() >= 8) {
            auto on = level(pins[0], 7, low);
            r.circle(world({x1 + 4, y0}), 1.8f, on == 1 ? color : on == 0 ? dim(color) : c.unknown);
        }
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
