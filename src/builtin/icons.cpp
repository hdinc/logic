#include "builder.h"
#include "builtin.h"

#include <cmath>
#include <functional>
#include <map>

namespace logic::builtin {

namespace {

using model::text_align;

// icons are drawn in a 24 by 24 box around the origin, y up, without pins:
// the body of the part, like a glyph

// text centered on y
void label(builder& b, glm::ivec2 at, const char* s, float size)
{
    b.text({at.x, at.y - (int)std::lround(size * 0.36f)}, s, size, text_align::center);
}

// a chip: a box with three ticks on each side and a label
void chip(builder& b, const char* name)
{
    b.rect({-7, -10}, {7, 10});
    for (int y : {6, 0, -6}) {
        b.line({-10, y}, {-7, y});
        b.line({7, y}, {10, y});
    }
    // one or two letters, more aren't readable at icon size
    label(b, {0, 0}, name, 9.0f);
}

// a box with a sign in it
void boxed(builder& b, const char* sign, float size = 13.0f)
{
    b.rect({-9, -9}, {9, 9});
    label(b, {0, 0}, sign, size);
}

void and_body(builder& b, int dx)
{
    b.polyline({{dx, 8}, {-8 + dx, 8}, {-8 + dx, -8}, {dx, -8}});
    b.arc({dx, 8}, {dx + 8, 0}, {dx, -8});
}

void or_body(builder& b, int dx)
{
    b.bezier({-9 + dx, 8}, {-4 + dx, 0}, {-9 + dx, -8});
    b.bezier({-9 + dx, 8}, {2 + dx, 8}, {9 + dx, 0});
    b.bezier({-9 + dx, -8}, {2 + dx, -8}, {9 + dx, 0});
}

// the extra back curve of exclusive gates at x
void xor_back(builder& b, int x)
{
    b.bezier({x, 8}, {x + 5, 0}, {x, -8});
}

void bubble(builder& b, int x, float radius = 2.5f)
{
    b.circle({x, 0}, radius);
}

// a buffer's triangle, its tip at x
void triangle(builder& b, int tip)
{
    b.polyline({{-8, 8}, {tip, 0}, {-8, -8}, {-8, 8}});
}

using icon_fn = std::function<void(builder&)>;

const std::map<std::string, icon_fn>& icons()
{
    static const std::map<std::string, icon_fn> m = {
        // logic gates
        {"and", [](builder& b) { and_body(b, 0); }},
        {"nand", [](builder& b) { and_body(b, -2); bubble(b, 8); }},
        {"or", [](builder& b) { or_body(b, 0); }},
        {"nor", [](builder& b) { or_body(b, -2); bubble(b, 9); }},
        {"xor", [](builder& b) { or_body(b, 2); xor_back(b, -10); }},
        {"xnor", [](builder& b) { or_body(b, -1); xor_back(b, -12); bubble(b, 10, 2.0f); }},
        {"buffer", [](builder& b) { triangle(b, 8); }},
        {"not", [](builder& b) { triangle(b, 5); bubble(b, 8); }},
        {"tristate", [](builder& b) { triangle(b, 8); b.line({0, -11}, {0, -4}); }},
        {"tristate_not", [](builder& b) { triangle(b, 5); bubble(b, 8); b.line({-1, -11}, {-1, -5}); }},

        // sources
        {"constant", [](builder& b) { b.circle({0, 0}, 10.0f); label(b, {0, 0}, "1", 11.0f); }},
        {"clock",
         [](builder& b) {
             b.circle({0, 0}, 10.0f);
             b.polyline({{-6, -3}, {-3, -3}, {-3, 3}, {0, 3}, {0, -3}, {3, -3}, {3, 3}, {6, 3}});
         }},

        {"random", [](builder& b) { chip(b, "?"); }},
        {"lfsr", [](builder& b) { chip(b, "LF"); }},

        // memory
        {"register", [](builder& b) { chip(b, "D"); }},
        {"counter", [](builder& b) { chip(b, "CT"); }},
        {"ram", [](builder& b) { chip(b, "RW"); }},
        {"axi_ram", [](builder& b) { chip(b, "M"); b.line({-10, -10}, {-10, 10}, model::bundle_stroke); }},
        {"axi_master", [](builder& b) { chip(b, "TM"); b.line({10, -10}, {10, 10}, model::bundle_stroke); }},
        {"rv32_cpu", [](builder& b) { chip(b, "RV"); b.line({10, -10}, {10, 10}, model::bundle_stroke); }},
        {"axi_crossbar",
            [](builder& b) {
                b.rect({-6, -10}, {6, 10});
                for (int y : {6, -6}) {
                    b.line({-11, y}, {-6, y}, model::bundle_stroke);
                    b.line({6, y}, {11, y}, model::bundle_stroke);
                }
                b.line({-6, 6}, {6, -6});
                b.line({-6, -6}, {6, 6});
            }},
        {"rom", [](builder& b) { chip(b, "R"); }},

        // arithmetic
        {"addsub", [](builder& b) { boxed(b, "+-", 10.0f); }},
        {"multiplier", [](builder& b) { boxed(b, "x"); }},
        {"divider", [](builder& b) { boxed(b, "/"); }},
        {"negator", [](builder& b) { boxed(b, "-x", 10.0f); }},
        {"comparator", [](builder& b) { boxed(b, "<>", 10.0f); }},
        {"shifter", [](builder& b) { boxed(b, "<<", 10.0f); }},
        {"extender",
         [](builder& b) {
             // a narrow bus into a wide one
             b.rect({-10, -4}, {-2, 4});
             b.rect({2, -9}, {10, 9});
             b.line({-2, 0}, {2, 0});
         }},

        // plexers
        {"mux", [](builder& b) { b.polyline({{-6, 10}, {6, 5}, {6, -5}, {-6, -10}, {-6, 10}}); }},
        {"demux", [](builder& b) { b.polyline({{6, 10}, {-6, 5}, {-6, -5}, {6, -10}, {6, 10}}); }},
        // the demux shape, the symbol has no input
        {"decoder", [](builder& b) { b.polyline({{6, 10}, {-6, 5}, {-6, -5}, {6, -10}, {6, 10}}); }},
        {"priority", [](builder& b) { chip(b, "PR"); }},
        {"popcount", [](builder& b) { boxed(b, "#1", 10.0f); }},
        {"bitselect",
         [](builder& b) {
             // a bracket picking bits out of a bus
             b.polyline({{-4, 10}, {-8, 10}, {-8, -10}, {-4, -10}});
             b.line({-4, 2}, {8, 2});
             b.line({-4, -2}, {8, -2});
             b.line({8, 2}, {8, -2});
         }},

        // wiring
        {"splitter",
         [](builder& b) {
             b.line({-10, 0}, {-3, 0});
             for (int y : {8, 0, -8})
                 b.line({-3, 0}, {9, y});
         }},
        {"breakout",
         [](builder& b) {
             b.line({-10, 0}, {-4, 0});
             b.line({-4, 8}, {-4, -8});
             for (int y : {8, 0, -8})
                 b.line({-4, y}, {9, y});
         }},
        {"tap",
         [](builder& b) {
             b.line({-10, -5}, {10, -5});
             b.polyline({{-3, -5}, {3, 3}, {10, 3}});
         }},
        {"tunnel", [](builder& b) { b.polyline({{-9, 0}, {-4, 5}, {9, 5}, {9, -5}, {-4, -5}, {-9, 0}}); }},
        {"pull",
         [](builder& b) {
             b.polyline({{0, 11}, {0, 7}, {4, 5}, {-4, 2}, {4, -1}, {-4, -4}, {0, -6}, {0, -8}});
             b.line({-5, -8}, {5, -8});
             b.line({-3, -10}, {3, -10});
         }},

        // input and output
        {"led",
         [](builder& b) {
             b.circle({-2, -2}, 7.0f);
             b.line({5, 5}, {9, 9});
             b.line({7, 1}, {11, 3});
             b.line({1, 7}, {3, 11});
         }},
        {"seg7_display", [](builder& b) { label(b, {-1, 0}, "8.", 16.0f); }},
        {"hex_display",
         [](builder& b) {
             b.rect({-9, -10}, {9, 10});
             label(b, {0, 0}, "F", 13.0f);
         }},
        {"button",
         [](builder& b) {
             b.line({-10, -4}, {-5, -4});
             b.line({5, -4}, {10, -4});
             b.line({-7, 2}, {7, 2});
             b.line({0, 2}, {0, 8});
             b.line({-3, 8}, {3, 8});
         }},
        {"switch",
         [](builder& b) {
             b.line({-10, -3}, {-5, -3});
             b.line({-5, -3}, {6, 5});
             b.line({6, -3}, {10, -3});
             b.circle({-5, -3}, 1.0f);
             b.circle({6, -3}, 1.0f);
         }},
        {"terminal",
         [](builder& b) {
             b.rect({-10, -8}, {10, 8});
             b.text({-7, -2}, ">_", 7.0f);
         }},
        {"framebuffer",
         [](builder& b) {
             // a screen with a few lit pixels
             b.rect({-10, -8}, {10, 8});
             b.rect({-6, 0}, {-2, 4});
             b.rect({2, -4}, {6, 0});
         }},
    };
    return m;
}

}

model::symbol primitive::icon() const
{
    return builtin::icon(kind());
}

model::symbol icon(const std::string& kind)
{
    auto& m = icons();
    auto it = m.find(kind);
    if (it == m.end()) return {};
    builder b;
    it->second(b);
    return b.finish().symbol;
}

}
