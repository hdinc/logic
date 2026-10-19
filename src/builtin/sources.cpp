#include "builder.h"
#include "builtin.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace logic::builtin {

namespace {

using model::direction;
using model::grid;

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

    void eval(std::span<sim::value> ports, sim::context&) override { ports[0] = v; }

private:
    sim::value v;
};

// starts low, low for `low` then high for `high`
class clock_component : public sim::component {
public:
    clock_component(sim::time high, sim::time low)
        : high(high)
        , low(low)
    {
    }

    void eval(std::span<sim::value> ports, sim::context& ctx) override
    {
        if (ctx.woken()) level = !level;
        // the first eval after reset starts the clock
        if (ctx.woken() || !started) {
            started = true;
            ctx.wake_after(level ? high : low);
        }
        ports[0] = sim::known(level, 1);
    }

    void reset() override
    {
        level = false;
        started = false;
    }

    void inspect(std::vector<sim::inspect_item>& out) const override { out.push_back({"level", sim::known(level, 1)}); }

    bool save_state(std::vector<uint8_t>& out) const override
    {
        out = {uint8_t(level), uint8_t(started)};
        return true;
    }

    bool load_state(std::span<const uint8_t> in) override
    {
        if (in.size() != 2) return false;
        level = in[0];
        started = in[1];
        return true;
    }

private:
    sim::time high, low;
    bool level = false;
    bool started = false;
};

class constant : public primitive {
public:
    std::string kind() const override { return "constant"; }
    std::string name() const override { return "Constant"; }
    std::string category() const override { return "Logic sources"; }
    std::string description() const override { return "a fixed value"; }

    std::vector<model::param_def> params() const override
    {
        return {
            model::int_param("width", "Bit width", 1, 1, model::max_width),
            model::int_param("value", "Value", 1, std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max(),
                "bits above the width are ignored"),
        };
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int width = (int)model::get_int(v, "width");
        auto str = format_value(model::get_int(v, "value"), width);

        // a box like an input port connector: 10 high, 15 from the pin,
        // wider for longer values. the model has no font metrics, digits are
        // about 0.55 of the size wide.
        constexpr float size = 7.0f;
        int text_w = (int)std::lround(str.size() * size * 0.55f);
        int w = std::max(10, text_w + 4);
        int right = grid - 15;

        builder b;
        b.rect({right - w, -5}, {right, 5});
        b.text({right - w + (w - text_w) / 2, -2}, str, size);
        b.line({right, 0}, {grid, 0});
        b.port("out", direction::output, width, {grid, 0}, "the value");
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
    std::string category() const override { return "Logic sources"; }
    std::string description() const override { return "toggles between 1 and 0 while simulating"; }

    std::vector<model::param_def> params() const override
    {
        return {
            model::int_param("frequency", "Frequency (Hz)", 1, 1, 1000000000, "full periods per simulated second"),
            model::int_param("duty", "Duty cycle (%)", 50, 1, 99, "part of the period the clock is 1"),
        };
    }

    model::cell_interface build(const model::param_values&) const override
    {
        // like an input port connector, wide enough for a pulse
        builder b;
        b.rect({-21, -5}, {-5, 5});
        b.polyline({{-19, -3}, {-15, -3}, {-15, 3}, {-11, 3}, {-11, -3}, {-7, -3}});
        b.line({-5, 0}, {grid, 0});
        b.port("clk", direction::output, 1, {grid, 0}, "starts at 0, toggles while the simulation runs");
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        auto period = std::max<sim::time>(2, sim::seconds / (sim::time)model::get_int(v, "frequency"));
        auto high = std::clamp<sim::time>(period * model::get_int(v, "duty") / 100, 1, period - 1);
        return std::make_unique<clock_component>(high, period - high);
    }
};

}

void add_sources(model::primitive_registry& r)
{
    r.add(std::make_unique<constant>());
    r.add(std::make_unique<clock>());
}

}
