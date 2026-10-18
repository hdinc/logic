#include "builder.h"
#include "builtin.h"

#include <algorithm>
#include <cstdlib>

namespace logic::builtin {

namespace {

using model::direction;
using model::grid;

// y of the inputs, top first, one grid apart. an even count leaves out the
// middle so every input is on the grid.
std::vector<int> input_ys(int n)
{
    std::vector<int> ys;
    for (int k = 0; k < n; k++) {
        int y = n / 2 - k;
        if (n % 2 == 0 && y <= 0) y--;
        ys.push_back(y * grid);
    }
    return ys;
}

void width_label(builder& b, int width, glm::ivec2 pos)
{
    if (width > 1) b.text(pos, std::to_string(width), 6.0f);
}

// output lead from the tip of the body at x to the pin one grid further,
// with a bubble if negated
void output(builder& b, int x, bool negate, int width)
{
    if (negate) {
        b.circle({x + 3, 0}, 3.0f);
        b.line({x + 6, 0}, {x + grid, 0});
    } else {
        b.line({x, 0}, {x + grid, 0});
    }
    b.port("out", direction::output, width, {x + grid, 0});
    width_label(b, width, {x + 2, 2});
}

class gate : public model::primitive {
public:
    enum op_t {
        and_op,
        or_op,
        xor_op
    };

    gate(std::string kind, std::string name, op_t op, bool negate, std::string description)
        : k(std::move(kind))
        , n(std::move(name))
        , d(std::move(description))
        , op(op)
        , negate(negate)
    {
    }

    std::string kind() const override { return k; }
    std::string name() const override { return n; }
    std::string description() const override { return d; }

    std::vector<model::param_def> params() const override
    {
        return {
            model::int_param("inputs", "Inputs", 2, 2, 32),
            model::int_param("width", "Bit width", 1, 1, 64, "the gate works on each bit"),
        };
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int inputs = (int)model::get_int(v, "inputs");
        int width = (int)model::get_int(v, "width");

        auto ys = input_ys(inputs);
        int max_y = std::abs(ys.front());

        // the body keeps its size, more inputs extend the back
        int h = std::clamp(max_y + grid, 2 * grid, 3 * grid);
        int back = -h;
        int ext = std::max(h, max_y);

        builder b;

        if (op == and_op) {
            b.line({back, ext}, {back, -ext});
            b.line({back, h}, {0, h});
            b.line({back, -h}, {0, -h});
            b.bezier({0, h}, {h, h}, {h, 0});
            b.bezier({h, 0}, {h, -h}, {0, -h});
        } else {
            b.bezier({back, h}, {back + h * 6 / 10, 0}, {back, -h});
            b.bezier({back, h}, {h * 3 / 10, h}, {h, 0});
            b.bezier({back, -h}, {h * 3 / 10, -h}, {h, 0});
            if (op == xor_op) b.bezier({back - 6, h}, {back - 6 + h * 6 / 10, 0}, {back - 6, -h});
            if (ext > h) {
                b.line({back, h}, {back, ext});
                b.line({back, -h}, {back, -ext});
            }
        }

        for (int i = 0; i < inputs; i++) {
            int y = ys[i];
            int x = back;
            // leads end at the curved back of or gates
            if (op != and_op && std::abs(y) < h) {
                float t = (1.0f - float(y) / h) / 2.0f;
                x = back + (int)std::lround(2.0f * t * (1.0f - t) * h * 6 / 10);
            }
            b.line({back - grid, y}, {x, y});
            b.port("in" + std::to_string(i), direction::input, width, {back - grid, y});
        }

        output(b, h, negate, width);
        return b.finish();
    }

private:
    std::string k, n, d;
    op_t op;
    bool negate;
};

class buffer : public model::primitive {
public:
    explicit buffer(bool negate)
        : negate(negate)
    {
    }

    std::string kind() const override { return negate ? "not" : "buffer"; }
    std::string name() const override { return negate ? "NOT" : "Buffer"; }
    std::string description() const override { return negate ? "inverts every bit" : "passes the input through"; }

    std::vector<model::param_def> params() const override
    {
        return {model::int_param("width", "Bit width", 1, 1, 64)};
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int width = (int)model::get_int(v, "width");

        builder b;
        b.polyline({{-grid, grid}, {grid, 0}, {-grid, -grid}, {-grid, grid}});
        b.line({-2 * grid, 0}, {-grid, 0});
        b.port("in", direction::input, width, {-2 * grid, 0});
        output(b, grid, negate, width);
        return b.finish();
    }

private:
    bool negate;
};

}

void add_gates(model::primitive_registry& r)
{
    r.add(std::make_unique<gate>("and", "AND", gate::and_op, false, "1 if all inputs are 1"));
    r.add(std::make_unique<gate>("or", "OR", gate::or_op, false, "1 if any input is 1"));
    r.add(std::make_unique<gate>("xor", "XOR", gate::xor_op, false, "1 if an odd number of inputs is 1"));
    r.add(std::make_unique<gate>("nand", "NAND", gate::and_op, true, "0 if all inputs are 1"));
    r.add(std::make_unique<gate>("nor", "NOR", gate::or_op, true, "0 if any input is 1"));
    r.add(std::make_unique<gate>("xnor", "XNOR", gate::xor_op, true, "1 if an even number of inputs is 1"));
    r.add(std::make_unique<buffer>(true));
    r.add(std::make_unique<buffer>(false));
}

}
