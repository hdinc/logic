#include "symbol.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace logic::model {

namespace {
template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
}

int point_count(const shape& s)
{
    return std::visit(overloaded{
                          [](const line&) { return 2; },
                          [](const bezier&) { return 3; },
                          [](const arc&) { return 3; },
                          [](const circle&) { return 2; },
                          [](const text&) { return 1; },
                      },
                      s.geom);
}

glm::ivec2 get_point(const shape& s, int i)
{
    return std::visit(overloaded{
                          [&](const line& l) { return i == 0 ? l.a : l.b; },
                          [&](const bezier& b) { return i == 0 ? b.a : (i == 1 ? b.b : b.c); },
                          [&](const arc& a) { return i == 0 ? a.a : (i == 1 ? a.b : a.c); },
                          [&](const circle& c) { return i == 0 ? c.center : c.center + glm::ivec2(glm::round(c.radius), 0); },
                          [&](const text& t) { return t.pos; },
                      },
                      s.geom);
}

void set_point(shape& s, int i, glm::ivec2 p)
{
    std::visit(overloaded{
                   [&](line& l) { (i == 0 ? l.a : l.b) = p; },
                   [&](bezier& b) { (i == 0 ? b.a : (i == 1 ? b.b : b.c)) = p; },
                   [&](arc& a) { (i == 0 ? a.a : (i == 1 ? a.b : a.c)) = p; },
                   [&](circle& c) {
                       if (i == 0)
                           c.center = p;
                       else
                           c.radius = glm::length(glm::vec2(p - c.center));
                   },
                   [&](text& t) { t.pos = p; },
               },
               s.geom);
}

std::vector<glm::vec2> arc_points(const arc& s)
{
    glm::dvec2 a = s.a, b = s.b, c = s.c;

    // center of the circle through the three points
    double d = 2.0 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
    if (std::abs(d) < 1e-9) return {glm::vec2(a), glm::vec2(b)};
    double aa = glm::dot(a, a), bb = glm::dot(b, b), cc = glm::dot(c, c);
    glm::dvec2 m{(aa * (b.y - c.y) + bb * (c.y - a.y) + cc * (a.y - b.y)) / d, (aa * (c.x - b.x) + bb * (a.x - c.x) + cc * (b.x - a.x)) / d};
    double r = glm::length(a - m);

    constexpr double tau = 2.0 * std::numbers::pi;
    auto angle = [&](glm::dvec2 p) { return std::atan2(p.y - m.y, p.x - m.x); };
    auto wrap = [&](double t) { return t - tau * std::floor(t / tau); };
    double ta = angle(a);
    // counter clockwise to b, unless c is on the other way
    double sweep = wrap(angle(b) - ta);
    if (wrap(angle(c) - ta) > sweep) sweep -= tau;

    int n = std::max(4, (int)std::ceil(std::abs(sweep) / tau * 72.0));
    std::vector<glm::vec2> r_points;
    r_points.push_back(glm::vec2(a));
    for (int k = 1; k < n; k++) {
        double t = ta + sweep * k / n;
        r_points.push_back(glm::vec2(m + r * glm::dvec2{std::cos(t), std::sin(t)}));
    }
    r_points.push_back(glm::vec2(b));
    return r_points;
}

shape* symbol::find_shape(id i)
{
    for (auto& s : shapes)
        if (s.id == i) return &s;
    return nullptr;
}

pin* symbol::find_pin(id i)
{
    for (auto& p : pins)
        if (p.id == i) return &p;
    return nullptr;
}

const pin* symbol::find_pin_of_port(id port) const
{
    for (auto& p : pins)
        if (p.port == port) return &p;
    return nullptr;
}

rect symbol::bounds() const
{
    bool first = true;
    rect r;

    auto add = [&](glm::vec2 p) {
        if (first) {
            r = {p, p};
            first = false;
        } else {
            r.expand(p);
        }
    };

    for (auto& s : shapes) {
        if (auto c = std::get_if<circle>(&s.geom)) {
            add(glm::vec2(c->center) - c->radius);
            add(glm::vec2(c->center) + c->radius);
        } else if (auto a = std::get_if<arc>(&s.geom)) {
            for (auto p : arc_points(*a))
                add(p);
        } else {
            for (int i = 0; i < point_count(s); i++)
                add(get_point(s, i));
        }
    }

    for (auto& p : pins)
        add(p.pos);

    if (first) return {{-grid, -grid}, {grid, grid}};

    return r;
}

namespace {

// the graph of fill_outlines: points and the segments between them
struct outline_graph {
    std::vector<glm::vec2> points;
    struct edge {
        int a, b;
        bool alive = true;
    };
    std::vector<edge> edges;
    // edges at each point
    std::vector<std::vector<int>> at;

    // the point at p, the same for points closer than a hundredth
    int point(glm::vec2 p)
    {
        for (int k = 0; k < (int)points.size(); k++)
            if (glm::distance(points[k], p) < 0.01f) return k;
        points.push_back(p);
        at.emplace_back();
        return (int)points.size() - 1;
    }

    void add(int a, int b)
    {
        if (a == b) return;
        edges.push_back({a, b});
        at[a].push_back((int)edges.size() - 1);
        at[b].push_back((int)edges.size() - 1);
    }

    void polyline(const std::vector<glm::vec2>& ps)
    {
        for (size_t k = 1; k < ps.size(); k++)
            add(point(ps[k - 1]), point(ps[k]));
    }

    int degree(int p) const
    {
        int n = 0;
        for (int e : at[p])
            n += edges[e].alive;
        return n;
    }

    int other(int e, int p) const { return edges[e].a == p ? edges[e].b : edges[e].a; }
};

std::vector<glm::vec2> bezier_points(const bezier& b)
{
    std::vector<glm::vec2> r;
    for (int k = 0; k <= 16; k++) {
        float t = k / 16.0f;
        r.push_back((1 - t) * (1 - t) * glm::vec2(b.a) + 2 * t * (1 - t) * glm::vec2(b.c) + t * t * glm::vec2(b.b));
    }
    return r;
}

}

std::vector<std::vector<glm::vec2>> fill_outlines(const symbol& s)
{
    std::vector<std::vector<glm::vec2>> r;
    outline_graph g;

    // the ends of all shapes split the lines they lie on
    std::vector<glm::vec2> ends;
    for (auto& sh : s.shapes) {
        if (auto l = std::get_if<line>(&sh.geom)) ends.insert(ends.end(), {glm::vec2(l->a), glm::vec2(l->b)});
        if (auto a = std::get_if<arc>(&sh.geom)) ends.insert(ends.end(), {glm::vec2(a->a), glm::vec2(a->b)});
        if (auto b = std::get_if<bezier>(&sh.geom)) ends.insert(ends.end(), {glm::vec2(b->a), glm::vec2(b->b)});
    }

    for (auto& sh : s.shapes) {
        if (auto l = std::get_if<line>(&sh.geom)) {
            glm::vec2 a = l->a, b = l->b;
            auto d = b - a;
            float len2 = glm::dot(d, d);
            if (len2 == 0) continue;
            // the line through the ends on it, in order
            std::vector<float> ts{0, 1};
            for (auto p : ends) {
                float t = glm::dot(p - a, d) / len2;
                if (t <= 0.001f || t >= 0.999f) continue;
                if (glm::distance(a + t * d, p) < 0.01f) ts.push_back(t);
            }
            std::sort(ts.begin(), ts.end());
            std::vector<glm::vec2> ps;
            for (float t : ts)
                ps.push_back(a + t * d);
            g.polyline(ps);
        } else if (auto a = std::get_if<arc>(&sh.geom)) {
            g.polyline(arc_points(*a));
        } else if (auto b = std::get_if<bezier>(&sh.geom)) {
            g.polyline(bezier_points(*b));
        } else if (auto c = std::get_if<circle>(&sh.geom)) {
            if (c->radius <= 0) continue;
            std::vector<glm::vec2> loop;
            for (int k = 0; k < 32; k++) {
                float t = k * 2.0f * std::numbers::pi_v<float> / 32;
                loop.push_back(glm::vec2(c->center) + c->radius * glm::vec2{std::cos(t), std::sin(t)});
            }
            r.push_back(std::move(loop));
        }
    }

    // what hangs off the outlines goes, end by end
    std::vector<int> open;
    for (int p = 0; p < (int)g.points.size(); p++)
        if (g.degree(p) == 1) open.push_back(p);
    while (!open.empty()) {
        int p = open.back();
        open.pop_back();
        for (int e : g.at[p]) {
            if (!g.edges[e].alive) continue;
            g.edges[e].alive = false;
            int q = g.other(e, p);
            if (g.degree(q) == 1) open.push_back(q);
        }
    }

    // the outer boundary of each group: from its leftmost point, always the
    // most clockwise turn, the outside stays on the right
    std::vector<bool> done(g.points.size(), false);
    for (int start = 0; start < (int)g.points.size(); start++) {
        if (done[start] || g.degree(start) == 0) continue;
        // the group of start, and its leftmost (then lowest) point
        std::vector<int> group{start}, stack{start};
        done[start] = true;
        while (!stack.empty()) {
            int p = stack.back();
            stack.pop_back();
            for (int e : g.at[p]) {
                if (!g.edges[e].alive) continue;
                int q = g.other(e, p);
                if (done[q]) continue;
                done[q] = true;
                group.push_back(q);
                stack.push_back(q);
            }
        }
        int first = start;
        for (int p : group) {
            auto a = g.points[p], b = g.points[first];
            if (a.x < b.x - 0.001f || (std::abs(a.x - b.x) <= 0.001f && a.y < b.y)) first = p;
        }

        std::vector<glm::vec2> outline;
        int p = first, came = -1;
        glm::vec2 dir{0, -1};
        int first_edge = -1;
        for (size_t step = 0; step < 4 * g.edges.size() + 4; step++) {
            int best = -1;
            float best_turn = 10;
            for (int e : g.at[p]) {
                if (!g.edges[e].alive) continue;
                // back the same way only at a dead end
                if (e == came && g.degree(p) > 1) continue;
                auto d = g.points[g.other(e, p)] - g.points[p];
                float turn = std::atan2(dir.x * d.y - dir.y * d.x, glm::dot(dir, d));
                if (turn < best_turn) {
                    best_turn = turn;
                    best = e;
                }
            }
            if (best < 0) break;
            if (best == first_edge && p == first) break;
            if (first_edge < 0) first_edge = best;
            outline.push_back(g.points[p]);
            int q = g.other(best, p);
            dir = g.points[q] - g.points[p];
            came = best;
            p = q;
        }
        if (outline.size() >= 3) r.push_back(std::move(outline));
    }
    return r;
}

}
