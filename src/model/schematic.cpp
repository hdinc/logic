#include "schematic.h"

#include <algorithm>
#include <map>
#include <set>

namespace logic::model {

wire* schematic::find_wire(id i)
{
    for (auto& w : wires)
        if (w.id == i) return &w;
    return nullptr;
}

instance* schematic::find_instance(id i)
{
    for (auto& in : instances)
        if (in.id == i) return &in;
    return nullptr;
}

namespace {

using span = std::pair<int, int>; // [first, second], first <= second

// merges overlapping or touching spans on one line
std::vector<span> merge_spans(std::vector<span> spans)
{
    std::sort(spans.begin(), spans.end());
    std::vector<span> r;
    for (auto& s : spans) {
        if (!r.empty() && s.first <= r.back().second)
            r.back().second = std::max(r.back().second, s.second);
        else
            r.push_back(s);
    }
    return r;
}

// splits spans at the given cut coordinates that lie strictly inside them
std::vector<span> split_spans(const std::vector<span>& spans, const std::set<int>& cuts)
{
    std::vector<span> r;
    for (auto s : spans) {
        for (auto it = cuts.upper_bound(s.first); it != cuts.end() && *it < s.second; ++it) {
            r.push_back({s.first, *it});
            s.first = *it;
        }
        r.push_back(s);
    }
    return r;
}

struct ivec2_less {
    bool operator()(glm::ivec2 a, glm::ivec2 b) const { return a.x != b.x ? a.x < b.x : a.y < b.y; }
};

}

std::vector<wire> normalize_wires(const std::vector<wire>& wires, const std::vector<glm::ivec2>& extra_points, id& next_id)
{
    std::map<int, std::vector<span>> hori; // y -> spans in x
    std::map<int, std::vector<span>> vert; // x -> spans in y
    std::vector<wire> diag;

    // horizontal wires are split at endpoints of non horizontal wires and vice
    // versa. endpoints of collinear wires don't split, they get merged.
    std::set<glm::ivec2, ivec2_less> hori_cuts(extra_points.begin(), extra_points.end());
    std::set<glm::ivec2, ivec2_less> vert_cuts(extra_points.begin(), extra_points.end());

    for (auto& w : wires) {
        if (w.a == w.b) continue;

        if (w.a.y == w.b.y) {
            hori[w.a.y].push_back({std::min(w.a.x, w.b.x), std::max(w.a.x, w.b.x)});
            vert_cuts.insert({w.a, w.b});
        } else if (w.a.x == w.b.x) {
            vert[w.a.x].push_back({std::min(w.a.y, w.b.y), std::max(w.a.y, w.b.y)});
            hori_cuts.insert({w.a, w.b});
        } else {
            diag.push_back(w);
            hori_cuts.insert({w.a, w.b});
            vert_cuts.insert({w.a, w.b});
        }
    }

    std::vector<wire> r;

    for (auto& [y, spans] : hori) {
        std::set<int> cuts;
        for (auto p : hori_cuts)
            if (p.y == y) cuts.insert(p.x);
        for (auto s : split_spans(merge_spans(spans), cuts))
            r.push_back({next_id++, {s.first, y}, {s.second, y}});
    }

    for (auto& [x, spans] : vert) {
        std::set<int> cuts;
        for (auto p : vert_cuts)
            if (p.x == x) cuts.insert(p.y);
        for (auto s : split_spans(merge_spans(spans), cuts))
            r.push_back({next_id++, {x, s.first}, {x, s.second}});
    }

    for (auto& w : diag) {
        bool duplicate = std::any_of(r.begin(), r.end(), [&](const wire& o) {
            return (o.a == w.a && o.b == w.b) || (o.a == w.b && o.b == w.a);
        });
        if (!duplicate) r.push_back({next_id++, w.a, w.b});
    }

    return r;
}

}
