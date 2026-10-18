#include "builder.h"
#include "builtin.h"

#include <cstdio>
#include <limits>

namespace logic::builtin {

namespace {

using model::direction;
using model::grid;

int round_up(int v, int step) { return (v + step - 1) / step * step; }

// value shown on the symbol: the bit for width 1, hex otherwise
std::string format_value(int64_t value, int width)
{
    auto u = (uint64_t)value;
    if (width < 64) u &= (uint64_t(1) << width) - 1;
    if (width == 1) return u ? "1" : "0";

    char buf[24];
    std::snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)u);
    return buf;
}

class constant_component : public sim::component {
public:
    explicit constant_component(sim::value v)
        : v(v)
    {
    }

    void eval(std::span<sim::value> ports) override { ports[0] = v; }

private:
    sim::value v;
};

// low for `low` ticks, then high for `high` ticks
class clock_component : public sim::component {
public:
    clock_component(int64_t high, int64_t low)
        : high(high)
        , low(low)
    {
    }

    void eval(std::span<sim::value> ports) override { ports[0] = sim::known(level(), 1); }

    bool tick() override
    {
        bool before = level();
        phase = (phase + 1) % (high + low);
        return level() != before;
    }

    void reset() override { phase = 0; }

private:
    bool level() const { return phase >= low; }

    int64_t high, low;
    int64_t phase = 0;
};

class constant : public primitive {
public:
    std::string kind() const override { return "constant"; }
    std::string name() const override { return "Constant"; }
    std::string description() const override { return "a fixed value"; }

    std::vector<model::param_def> params() const override
    {
        return {
            model::int_param("width", "Bit width", 1, 1, 64),
            model::int_param("value", "Value", 1, std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max(),
                "bits above the width are ignored"),
        };
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int width = (int)model::get_int(v, "width");
        auto str = format_value(model::get_int(v, "value"), width);

        // the model has no font metrics, about 0.6 of the size per character
        constexpr float size = 10.0f;
        int text_w = (int)(str.size() * size * 0.6f);
        int w = std::max(2 * grid, round_up(text_w + 6, grid));

        builder b;
        b.rect({-w, -grid}, {0, grid});
        b.text({-w + (w - text_w) / 2, -4}, str, size);
        b.line({0, 0}, {grid, 0});
        b.port("out", direction::output, width, {grid, 0});
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        int width = (int)model::get_int(v, "width");
        return std::make_unique<constant_component>(sim::known((uint64_t)model::get_int(v, "value"), width));
    }
};

class clock : public primitive {
public:
    std::string kind() const override { return "clock"; }
    std::string name() const override { return "Clock"; }
    std::string description() const override { return "toggles between 1 and 0 while simulating"; }

    std::vector<model::param_def> params() const override
    {
        return {
            model::int_param("high", "High ticks", 1, 1, 1000000, "simulation ticks the clock stays 1"),
            model::int_param("low", "Low ticks", 1, 1, 1000000, "simulation ticks the clock stays 0"),
        };
    }

    model::cell_interface build(const model::param_values&) const override
    {
        builder b;
        b.rect({-3 * grid, -grid}, {0, grid});
        b.polyline({{-26, -5}, {-20, -5}, {-20, 5}, {-12, 5}, {-12, -5}, {-4, -5}});
        b.line({0, 0}, {grid, 0});
        b.port("clk", direction::output, 1, {grid, 0});
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<clock_component>(model::get_int(v, "high"), model::get_int(v, "low"));
    }
};

}

void add_sources(model::primitive_registry& r)
{
    r.add(std::make_unique<constant>());
    r.add(std::make_unique<clock>());
}

}
