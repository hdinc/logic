#include "builder.h"
#include "builtin.h"

namespace logic::builtin {

namespace {

using model::direction;
using model::grid;
using sim::value;

// d, clk, q. q takes d on the rising edge of clk, starts at 0.
class register_component : public sim::component {
public:
    explicit register_component(int width)
        : width(width)
    {
        reset();
    }

    void eval(std::span<value> ports) override
    {
        auto& clk = ports[1];
        bool rising = prev_clk == sim::known(0, 1) && clk == sim::known(1, 1);
        prev_clk = clk;

        if (rising) {
            auto& d = ports[0];
            auto x = (d.x | d.z) & sim::mask(width);
            q = {d.bits & ~x, x, 0};
        }
        ports[2] = q;
    }

    void reset() override
    {
        q = sim::known(0, width);
        prev_clk = sim::floating(1);
    }

private:
    int width;
    value q;
    value prev_clk;
};

class reg : public primitive {
public:
    std::string kind() const override { return "register"; }
    std::string name() const override { return "Register"; }
    std::string category() const override { return "Memory"; }
    std::string description() const override { return "stores D on the rising edge of the clock"; }

    std::vector<model::param_def> params() const override
    {
        return {model::int_param("width", "Bit width", 8, 1, 64)};
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int width = (int)model::get_int(v, "width");

        builder b;
        b.rect({-2 * grid, -3 * grid}, {2 * grid, 3 * grid});

        b.line({-3 * grid, grid}, {-2 * grid, grid});
        b.text({-18, 7}, "D", 6.0f);
        b.port("d", direction::input, width, {-3 * grid, grid}, "data, stored on the rising edge of clk");

        // clock input marked with a triangle
        b.line({-3 * grid, -grid}, {-2 * grid, -grid});
        b.polyline({{-2 * grid, -grid + 4}, {-14, -grid}, {-2 * grid, -grid - 4}});
        b.port("clk", direction::input, 1, {-3 * grid, -grid}, "clock, d is stored when it goes from 0 to 1");

        b.line({2 * grid, grid}, {3 * grid, grid});
        b.text({12, 7}, "Q", 6.0f);
        b.port("q", direction::output, width, {3 * grid, grid}, "the stored value, 0 after reset");

        if (width > 1) b.text({-6, -26}, std::to_string(width), 6.0f);
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<register_component>((int)model::get_int(v, "width"));
    }
};

}

void add_memory(model::primitive_registry& r)
{
    r.add(std::make_unique<reg>());
}

}
