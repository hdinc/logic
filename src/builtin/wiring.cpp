#include "../model/workspace.h"
#include "builder.h"
#include "builtin.h"

#include <cctype>
#include <cmath>
#include <optional>
#include <set>

namespace logic::builtin {

namespace {

using model::direction;
using model::grid;

// bits [lo, hi] of the bus
struct range {
    int lo, hi;
    int width() const { return hi - lo + 1; }
};

// "7:4, 3:0" or "0-3, 4-7", one range or bit per end. null if it's not
// valid for the width.
std::optional<std::vector<range>> parse_ranges(const std::string& s, int width)
{
    std::vector<range> r;
    size_t i = 0;

    auto skip_space = [&] {
        while (i < s.size() && std::isspace((unsigned char)s[i]))
            i++;
    };
    auto number = [&]() -> std::optional<int> {
        skip_space();
        size_t start = i;
        int v = 0;
        while (i < s.size() && std::isdigit((unsigned char)s[i]) && v < 1000)
            v = v * 10 + (s[i++] - '0');
        if (i == start) return std::nullopt;
        return v;
    };

    skip_space();
    if (i == s.size()) return std::nullopt;

    while (true) {
        auto a = number();
        if (!a) return std::nullopt;
        int b = *a;
        skip_space();
        if (i < s.size() && (s[i] == ':' || s[i] == '-')) {
            i++;
            auto n = number();
            if (!n) return std::nullopt;
            b = *n;
        }
        range x{std::min(*a, b), std::max(*a, b)};
        if (x.hi >= width || r.size() == 64) return std::nullopt;
        r.push_back(x);

        skip_space();
        if (i == s.size()) break;
        if (s[i] != ',') return std::nullopt;
        i++;
    }
    return r;
}

// consecutive ranges from bit 0, the first ends get the remaining bits
std::vector<range> even_ranges(int width, int fanout)
{
    fanout = std::min(fanout, width);
    std::vector<range> r;
    int lo = 0;
    for (int k = 0; k < fanout; k++) {
        int w = width / fanout + (k < width % fanout ? 1 : 0);
        r.push_back({lo, lo + w - 1});
        lo += w;
    }
    return r;
}

std::string label(range x)
{
    return x.width() == 1 ? std::to_string(x.lo) : std::to_string(x.hi) + ":" + std::to_string(x.lo);
}

// splits a bus into ends of fewer bits, or merges them into a bus. it's
// only wiring: values go both ways without delay.
class splitter : public primitive {
public:
    std::string kind() const override { return "splitter"; }
    std::string name() const override { return "Splitter"; }
    std::string category() const override { return "Wiring"; }
    std::string description() const override { return "connects bits of a bus to narrower ends, works in both directions"; }

    std::vector<model::param_def> params() const override
    {
        return {
            model::int_param("width", "Bus width", 2, 1, model::max_width),
            model::int_param("fanout", "Fan out", 2, 1, 64, "number of ends when Bits is empty, the bits are split evenly from bit 0"),
            model::text_param("bits", "Bits", "", "bits of each end instead of an even split, e.g. \"3:0, 7:4\" or \"0, 1, 7:2\""),
        };
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int width = (int)model::get_int(v, "width");
        auto ranges = parse_ranges(model::get_string(v, "bits"), width);
        if (!ranges) ranges = even_ranges(width, (int)model::get_int(v, "fanout"));
        int n = (int)ranges->size();

        // bus on the left, a spine going down, the ends to the right
        builder b;
        // bus lines as thick as bus wires
        auto stroke = [](int bits) { return bits > 1 ? model::bus_stroke : 0.0f; };
        b.line({0, 0}, {grid, -grid}, stroke(width));
        if (n > 1) b.line({grid, -grid}, {grid, -n * grid}, stroke(width));
        b.port("bus", direction::inout, width, {0, 0}, "the whole bus");

        for (int k = 0; k < n; k++) {
            int y = -(k + 1) * grid;
            auto& x = (*ranges)[k];
            b.line({grid, y}, {2 * grid, y}, stroke(x.width()));
            // above the line, clear of a thick one
            b.text({grid + 3, y + 3}, label(x), 4.5f);
            b.port("end" + std::to_string(k), direction::inout, x.width(), {2 * grid, y}, "bits " + label(x) + " of the bus");
        }

        auto r = b.finish();
        for (int k = 0; k < n; k++)
            r.links.push_back({0, (*ranges)[k].lo, k + 1, 0, (*ranges)[k].width()});
        return r;
    }

    std::string check(const model::param_values& v) const override
    {
        auto bits = model::get_string(v, "bits");
        if (bits.empty() || parse_ranges(bits, (int)model::get_int(v, "width"))) return {};
        return "bits are not valid for the bus width, split evenly";
    }

    // pure wiring, see the links
    bool is_wiring() const override { return true; }
    std::unique_ptr<sim::component> create(const model::param_values&) const override { return nullptr; }
};

// pulls the bits of a net that nothing drives to 0 or 1, other drivers win
class pull_resistor : public primitive {
public:
    std::string kind() const override { return "pull"; }
    std::string name() const override { return "Pull resistor"; }
    std::string category() const override { return "Wiring"; }
    std::string description() const override { return "gives floating (z) bits of the net a weak 0 or 1"; }

    std::vector<model::param_def> params() const override
    {
        return {
            model::choice_param("direction", "Direction", {"down", "up"}, "down", "pull down to 0 or up to 1"),
            model::int_param("width", "Bit width", 1, 1, model::max_width),
        };
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        bool up = model::get_string(v, "direction") == "up";
        int width = (int)model::get_int(v, "width");
        // y is up: the resistor goes from the pin towards the supply
        int s = up ? 1 : -1;

        builder b;
        b.line({0, 0}, {0, s * 5});
        b.polyline({{0, s * 5}, {4, s * 7}, {-4, s * 11}, {4, s * 15}, {-4, s * 19}, {4, s * 23}, {0, s * 25}});
        b.line({0, s * 25}, {0, s * 30});
        if (up) {
            b.line({-6, 30}, {6, 30});
            b.text({-3, 36}, "1", 6.0f);
        } else {
            b.line({-6, -30}, {6, -30});
            b.line({-4, -33}, {4, -33});
            b.line({-2, -36}, {2, -36});
        }
        b.port("pin", direction::inout, width, {0, 0}, up ? "floating bits become 1" : "floating bits become 0");

        auto r = b.finish();
        r.pulls.push_back({0, up});
        return r;
    }

    // works through the pulls of its interface
    bool is_wiring() const override { return true; }
    std::unique_ptr<sim::component> create(const model::param_values&) const override { return nullptr; }
};

// the bundle type of a breakout or tap and the fields it shows
struct bundle_view {
    model::id id = model::no_id;
    bool flipped = false;
    model::bundle_layout layout;
    // the fields in the order of the parameter, unknown and repeated ones
    // dropped
    std::vector<const model::bundle_node*> ends;
    std::vector<std::string> unknown;
    // the type is set and found
    bool valid = false;

    bundle_view(const model::param_values& v, const model::build_context& ctx, bool one)
    {
        id = model::parse_bundle_ref(model::get_string(v, "bundle"));
        flipped = model::get_bool(v, "flipped");
        if (id == model::no_id || !ctx.ws) return;
        layout = ctx.ws->layout_of(id, flipped);
        if (!layout) return;
        valid = true;

        auto paths = model::split_fields(model::get_string(v, one ? "field" : "fields"));
        if (one && paths.size() > 1) paths.resize(1);
        // all fields of the bundle itself
        if (paths.empty() && !one) {
            for (auto& n : layout.nodes)
                if (n.depth == 0) ends.push_back(&n);
            return;
        }
        std::set<std::string> seen;
        for (auto& p : paths) {
            auto n = layout.find(p);
            if (!n)
                unknown.push_back(p);
            else if (seen.insert(p).second)
                ends.push_back(n);
        }
    }

    // a problem to show for the parameters, empty if there is none
    std::string check(const model::build_context& ctx, bool one) const
    {
        if (id == model::no_id) return "choose a bundle type";
        if (!ctx.ws) return {};
        if (!layout) return layout.error;
        if (!unknown.empty()) return "the bundle has no field " + model::join_fields(unknown);
        if (one && ends.empty()) return "choose a field";
        return {};
    }

    // the bundle side of the part, plain bits if the type is unknown
    model::port& bundle_port(builder& b, const model::build_context& ctx, glm::ivec2 pin) const
    {
        auto label = ctx.ws && id != model::no_id ? ctx.ws->bundle_label(id, flipped) : std::string("bundle");
        auto& p = b.port("bundle", direction::inout, valid ? std::max(layout.width, 1) : 1, pin, "the whole bundle, " + label);
        if (valid) {
            p.bundle = id;
            p.flipped = flipped;
        }
        return p;
    }

    // an end for a field: the field's bits, or a bundle port of a nested bundle
    void end_port(builder& b, const model::build_context& ctx, const model::bundle_node& n, glm::ivec2 pin) const
    {
        std::string what = n.bundle != model::no_id ? "bundle " + ctx.ws->bundle_label(n.bundle, n.flipped) : std::string(model::to_string(n.dir));
        auto& p = b.port(n.path, direction::inout, std::max(n.width, 1), pin, "field " + n.path + ", " + what);
        if (n.bundle != model::no_id) {
            p.bundle = n.bundle;
            p.flipped = n.flipped;
        }
    }
};

// lines of a field as thick as its wires
float stroke(const model::bundle_node& n)
{
    return n.bundle != model::no_id ? model::bundle_stroke : n.width > 1 ? model::bus_stroke
                                                                         : 0.0f;
}

// approximate width of a label, the model doesn't know font metrics
int label_grids(const std::string& s, float size)
{
    return std::max(1, (int)std::ceil((s.size() * size * 0.6f + 6.0f) / grid));
}

std::vector<model::param_def> bundle_params(bool one)
{
    std::vector<model::param_def> r{
        model::bundle_param("bundle", "Bundle", "the bundle type"),
        model::bool_param("flipped", "Flipped", false, "the flipped view of the bundle, inputs and outputs swapped"),
    };
    if (one)
        r.push_back(model::fields_param("field", "Field", "bundle", true, "the field to tap, e.g. \"valid\" or \"aw.ready\""));
    else
        r.push_back(model::fields_param("fields", "Fields", "bundle", false, "the fields to show, separated by ',', e.g. \"valid, data\" or \"aw.valid\". empty for all fields of the bundle"));
    return r;
}

// connects fields of a bundle to their own ends, or puts a bundle together
// from them. it's only wiring like a splitter.
class breakout : public primitive {
public:
    std::string kind() const override { return "breakout"; }
    std::string name() const override { return "Bundle breakout"; }
    std::string category() const override { return "Wiring"; }
    std::string description() const override { return "connects fields of a bundle to their own ends, works in both directions"; }

    std::vector<model::param_def> params() const override { return bundle_params(false); }
    bool uses_bundles() const override { return true; }

    model::cell_interface build(const model::param_values& v) const override { return build(v, {}); }

    model::cell_interface build(const model::param_values& v, const model::build_context& ctx) const override
    {
        bundle_view view(v, ctx, false);
        int n = (int)view.ends.size();
        constexpr float label_size = 5.0f;

        // bundle on the left, a spine going down, the ends to the right as
        // long as their labels
        int len = 1;
        for (auto e : view.ends)
            len = std::max(len, label_grids(e->path, label_size));

        builder b;
        b.line({0, 0}, {grid, -grid}, model::bundle_stroke);
        if (n > 1) b.line({grid, -grid}, {grid, -n * grid}, model::bundle_stroke);
        // the type under the bundle pin, clear of the instance name above
        auto type = ctx.ws && view.id != model::no_id ? ctx.ws->bundle_label(view.id, view.flipped) : std::string("bundle");
        b.text({-label_grids(type, label_size) * grid + 4, -8}, type, label_size);
        view.bundle_port(b, ctx, {0, 0});

        for (int k = 0; k < n; k++) {
            int y = -(k + 1) * grid;
            auto e = view.ends[k];
            b.line({grid, y}, {(1 + len) * grid, y}, stroke(*e));
            b.text({grid + 3, y + 3}, e->path, label_size);
            view.end_port(b, ctx, *e, {(1 + len) * grid, y});
        }

        auto r = b.finish();
        for (int k = 0; k < n; k++)
            r.links.push_back({0, view.ends[k]->offset, k + 1, 0, view.ends[k]->width});
        return r;
    }

    std::string check(const model::param_values& v, const model::build_context& ctx) const override { return bundle_view(v, ctx, false).check(ctx, false); }

    // pure wiring, see the links
    bool is_wiring() const override { return true; }
    std::unique_ptr<sim::component> create(const model::param_values&) const override { return nullptr; }
};

// one field of a bundle, placed on a bundle wire
class tap : public primitive {
public:
    std::string kind() const override { return "tap"; }
    std::string name() const override { return "Bundle tap"; }
    std::string category() const override { return "Wiring"; }
    std::string description() const override { return "connects one field of a bundle wire, works in both directions"; }

    std::vector<model::param_def> params() const override { return bundle_params(true); }
    bool uses_bundles() const override { return true; }

    model::cell_interface build(const model::param_values& v) const override { return build(v, {}); }

    model::cell_interface build(const model::param_values& v, const model::build_context& ctx) const override
    {
        bundle_view view(v, ctx, true);
        constexpr float label_size = 5.0f;

        // a dot on the bundle wire, a short diagonal and the field's end
        builder b;
        b.circle({0, 0}, 2.5f);
        view.bundle_port(b, ctx, {0, 0});
        if (view.ends.empty()) {
            b.text({3, 3}, "?", label_size);
            return b.finish();
        }

        auto e = view.ends[0];
        auto label = "." + e->path;
        int len = label_grids(label, label_size);
        b.line({0, 0}, {grid, grid}, stroke(*e));
        b.line({grid, grid}, {(1 + len) * grid, grid}, stroke(*e));
        b.text({grid + 3, grid + 3}, label, label_size);
        view.end_port(b, ctx, *e, {(1 + len) * grid, grid});

        auto r = b.finish();
        r.links.push_back({0, e->offset, 1, 0, e->width});
        return r;
    }

    std::string check(const model::param_values& v, const model::build_context& ctx) const override { return bundle_view(v, ctx, true).check(ctx, true); }

    bool is_wiring() const override { return true; }
    std::unique_ptr<sim::component> create(const model::param_values&) const override { return nullptr; }
};
// connects its pin to every tunnel of the same name in the schematic, like
// an invisible wire. it takes the width of its net.
class tunnel : public primitive {
public:
    std::string kind() const override { return "tunnel"; }
    std::string name() const override { return "Tunnel"; }
    std::string category() const override { return "Wiring"; }
    std::string description() const override { return "connects to all tunnels with the same name in the schematic"; }

    std::vector<model::param_def> params() const override
    {
        return {model::text_param("name", "Name", "net", "tunnels with the same name are connected")};
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        auto label = model::get_string(v, "name");
        if (label.empty()) label = "?";

        // a small tag pointing at the pin, less than a grid high so tunnels
        // stack a grid apart. the name beside it, away from the pin.
        builder b;
        b.polyline({{0, 0}, {4, 4}, {grid + 5, 4}, {grid + 5, -4}, {4, -4}, {0, 0}});
        b.text({grid + 8, -2}, label, 6.0f);
        b.port("pin", direction::inout, 0, {0, 0}, "connected to the tunnels named " + label);

        auto r = b.finish();
        r.tunnel = model::get_string(v, "name");
        return r;
    }

    std::string check(const model::param_values& v) const override
    {
        return model::get_string(v, "name").empty() ? "give the tunnel a name" : std::string();
    }

    bool is_wiring() const override { return true; }
    std::unique_ptr<sim::component> create(const model::param_values&) const override { return nullptr; }
};

}

void add_wiring(model::primitive_registry& r)
{
    r.add(std::make_unique<splitter>());
    r.add(std::make_unique<breakout>());
    r.add(std::make_unique<tap>());
    r.add(std::make_unique<tunnel>());
    r.add(std::make_unique<pull_resistor>());
}

}
