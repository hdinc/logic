// the canvas font: distance fields (inside and outside of every glyph, sharp
// corners) and the curves large text is drawn from. no opengl needed.
//
// usage: font_test font.ttf... (missing fonts are skipped)

#include "../Hd/src/Hd/Msdf.h"
#include "../Hd/src/Hd/Outline.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

namespace {

constexpr float pixel_height = 48;
constexpr float range = 6;

struct segment {
    glm::vec2 a, b;
};

// the outline in font units as line segments
std::vector<segment> flatten(const stbtt_fontinfo& info, int glyph)
{
    stbtt_vertex* v = nullptr;
    int n = stbtt_GetGlyphShape(&info, glyph, &v);
    std::vector<segment> r;
    glm::vec2 p {}, first {};
    auto close = [&] {
        if (p != first) r.push_back({p, first});
    };
    for (int k = 0; k < n; k++) {
        auto& e = v[k];
        glm::vec2 q(e.x, e.y), c(e.cx, e.cy), c1(e.cx1, e.cy1);
        if (e.type == STBTT_vmove) {
            if (k > 0) close();
            first = q;
        } else if (e.type == STBTT_vline) {
            r.push_back({p, q});
        } else {
            glm::vec2 prev = p;
            for (int i = 1; i <= 32; i++) {
                float t = i / 32.0f, u = 1 - t;
                glm::vec2 s = e.type == STBTT_vcurve ? u * u * p + 2 * u * t * c + t * t * q
                                                     : u * u * u * p + 3 * u * u * t * c + 3 * u * t * t * c1 + t * t * t * q;
                r.push_back({prev, s});
                prev = s;
            }
        }
        p = q;
    }
    if (n > 0) close();
    stbtt_FreeShape(&info, v);
    return r;
}

// nonzero winding number of the outline around p
int winding(const std::vector<segment>& outline, glm::vec2 p)
{
    int w = 0;
    for (auto& s : outline) {
        if ((s.a.y <= p.y) == (s.b.y <= p.y)) continue;
        float x = s.a.x + (p.y - s.a.y) / (s.b.y - s.a.y) * (s.b.x - s.a.x);
        if (x > p.x) w += s.b.y > s.a.y ? 1 : -1;
    }
    return w;
}

float distance(const std::vector<segment>& outline, glm::vec2 p)
{
    float d = 1e30f;
    for (auto& s : outline) {
        glm::vec2 ab = s.b - s.a;
        float t = glm::clamp(glm::dot(p - s.a, ab) / std::max(glm::dot(ab, ab), 1e-12f), 0.0f, 1.0f);
        d = std::min(d, glm::length(p - (s.a + ab * t)));
    }
    return d;
}

float median(const unsigned char* c)
{
    return std::max(std::min(c[0], c[1]), std::min(std::max(c[0], c[1]), c[2])) / 255.0f;
}

// the field like the shader reads it: channels interpolated, then the median
float sample(const Hd::msdf::Field& f, glm::vec2 pixel)
{
    glm::vec2 q = pixel - 0.5f;
    int x = (int)std::floor(q.x), y = (int)std::floor(q.y);
    float fx = q.x - x, fy = q.y - y;
    auto at = [&](int i, int j, int k) {
        i = std::clamp(i, 0, f.w - 1);
        j = std::clamp(j, 0, f.h - 1);
        return f.rgb[(j * f.w + i) * 3 + k] / 255.0f;
    };
    float c[3];
    for (int k = 0; k < 3; k++)
        c[k] = (at(x, y, k) * (1 - fx) + at(x + 1, y, k) * fx) * (1 - fy) + (at(x, y + 1, k) * (1 - fx) + at(x + 1, y + 1, k) * fx) * fy;
    return std::max(std::min(c[0], c[1]), std::min(std::max(c[0], c[1]), c[2]));
}

// every texel clearly inside or outside the outline is on the right side of 0.5
void test_signs(const stbtt_fontinfo& info, const char* name)
{
    float scale = stbtt_ScaleForPixelHeight(&info, pixel_height);
    int glyphs = 0, wrong_glyphs = 0;
    for (uint32_t cp = 33; cp < 0x250; cp++) {
        if (cp >= 127 && cp < 160) continue;
        int g = stbtt_FindGlyphIndex(&info, (int)cp);
        if (!g) continue;
        auto f = Hd::msdf::generate(info, g, scale, range);
        if (f.w == 0) continue;
        auto outline = flatten(info, g);
        glyphs++;

        int wrong = 0;
        for (int y = 0; y < f.h; y++)
            for (int x = 0; x < f.w; x++) {
                glm::vec2 p = (f.origin + glm::vec2(x + 0.5f, y + 0.5f)) / scale;
                if (distance(outline, p) * scale < 1.5f) continue;
                bool inside = winding(outline, p) != 0;
                bool drawn = median(&f.rgb[(y * f.w + x) * 3]) > 0.5f;
                if (inside != drawn) wrong++;
            }
        if (wrong) {
            std::printf("%s: U+%04X has %d texels on the wrong side\n", name, cp, wrong);
            wrong_glyphs++;
        }
    }
    std::printf("%s: %d glyphs checked\n", name, glyphs);
    CHECK(glyphs > 50);
    CHECK(wrong_glyphs == 0);
}

// the outer corner of an L stays a corner when enlarged
void test_corner(const stbtt_fontinfo& info)
{
    float scale = stbtt_ScaleForPixelHeight(&info, pixel_height);
    int g = stbtt_FindGlyphIndex(&info, 'L');
    auto f = Hd::msdf::generate(info, g, scale, range);
    int x0, y0, x1, y1;
    stbtt_GetGlyphBox(&info, g, &x0, &y0, &x1, &y1);
    glm::vec2 corner = glm::vec2(x0, y0) * scale - f.origin;
    // a quarter pixel along the diagonal, 1/8 of a pixel on the screen at 8x
    CHECK(sample(f, corner + glm::vec2(0.25f)) > 0.5f);
    CHECK(sample(f, corner - glm::vec2(0.25f)) < 0.5f);
}

// the curves fill what stb_truetype's rasterizer fills, hairlines too
void test_curves(const stbtt_fontinfo& info, const char* name)
{
    float scale = stbtt_ScaleForPixelHeight(&info, 300);
    int wrong_glyphs = 0;
    for (uint32_t cp = 33; cp < 0x250; cp++) {
        if (cp >= 127 && cp < 160) continue;
        int g = stbtt_FindGlyphIndex(&info, (int)cp);
        if (!g) continue;
        auto curves = Hd::outline::curves(info, g);
        int w, h, x0, y0;
        unsigned char* bitmap = stbtt_GetGlyphBitmap(&info, scale, scale, g, &w, &h, &x0, &y0);
        if (!bitmap) continue;

        // pixels fully inside or outside, like all their neighbors
        auto solid = [&](int x, int y) {
            for (int j = std::max(y - 1, 0); j <= std::min(y + 1, h - 1); j++)
                for (int i = std::max(x - 1, 0); i <= std::min(x + 1, w - 1); i++)
                    if (bitmap[j * w + i] != bitmap[y * w + x]) return false;
            return bitmap[y * w + x] == 0 || bitmap[y * w + x] == 255;
        };
        int wrong = 0;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                if (!solid(x, y)) continue;
                // bitmap rows go down
                glm::vec2 p((x0 + x + 0.5f) / scale, -(y0 + y + 0.5f) / scale);
                if ((Hd::outline::winding(curves, p) != 0) != (bitmap[y * w + x] == 255)) wrong++;
            }
        stbtt_FreeBitmap(bitmap, nullptr);
        if (wrong) {
            std::printf("%s: curves of U+%04X differ in %d pixels\n", name, cp, wrong);
            wrong_glyphs++;
        }
    }
    CHECK(wrong_glyphs == 0);
}

std::vector<unsigned char> read(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}

int main(int argc, char** argv)
{
    int fonts = 0;
    for (int k = 1; k < argc; k++) {
        auto data = read(argv[k]);
        stbtt_fontinfo info;
        if (data.empty() || !stbtt_InitFont(&info, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0))) {
            std::printf("skipped %s\n", argv[k]);
            continue;
        }
        fonts++;
        test_signs(info, argv[k]);
        test_curves(info, argv[k]);
        if (k == 1) test_corner(info);
    }
    if (fonts == 0) return 77;

    if (failures) std::printf("%d failures\n", failures);
    else std::printf("all passed\n");
    return failures ? 1 : 0;
}
