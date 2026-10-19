#include "builder.h"
#include "builtin.h"

#include <cctype>
#include <optional>

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
            model::int_param("width", "Bus width", 2, 1, 64),
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
    std::unique_ptr<sim::component> create(const model::param_values&) const override { return nullptr; }
};

}

void add_wiring(model::primitive_registry& r)
{
    r.add(std::make_unique<splitter>());
}

}
