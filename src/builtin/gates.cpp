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

using sim::value;

enum class op_t {
    and_op,
    or_op,
    xor_op
};

// inputs first, the output last. inputs that are completely z (usually not
// connected) are ignored like in logisim, other x and z bits make the
// result unknown unless it's decided by the known bits.
class gate_component : public sim::component {
public:
    gate_component(op_t op, bool negate, int inputs, int width)
        : op(op)
        , negate(negate)
        , inputs(inputs)
        , width(width)
    {
    }

    void eval(std::span<value> ports, sim::context&) override
    {
        bool any = false;
        for (int k = 0; k < inputs; k++)
            any = any || !ports[k].fully_floating();

        value r = sim::unknown(width);
        if (any) {
            for (int w = 0; w < r.words(); w++) {
                auto m = sim::word_mask(width, w);
                uint64_t one = 0, zero = 0, unk = 0, parity = 0;
                for (int k = 0; k < inputs; k++) {
                    auto& v = ports[k];
                    if (v.fully_floating()) continue;
                    auto u = v.unknown_bits(w) & m;
                    unk |= u;
                    one |= v.bits(w) & ~u;
                    zero |= ~v.bits(w) & ~u & m;
                    parity ^= v.bits(w) & ~u;
                }

                uint64_t bits = 0, x = 0;
                switch (op) {
                case op_t::and_op: bits = ~zero & ~unk & m, x = unk & ~zero; break;
                case op_t::or_op: bits = one, x = unk & ~one; break;
                case op_t::xor_op: bits = parity & ~unk, x = unk; break;
                }
                if (negate) bits = ~bits & ~x & m;
                r.set_word(w, bits, x, 0);
            }
        }

        ports[inputs] = std::move(r);
    }

private:
    op_t op;
    bool negate;
    int inputs;
    int width;
};

// in, out
class buffer_component : public sim::component {
public:
    buffer_component(bool negate, int width)
        : negate(negate)
        , width(width)
    {
    }

    void eval(std::span<value> ports, sim::context&) override
    {
        auto& in = ports[0];
        value r(width, 0);
        for (int w = 0; w < r.words(); w++) {
            auto x = in.unknown_bits(w);
            r.set_word(w, negate ? ~in.bits(w) & ~x : in.bits(w), x, 0);
        }
        ports[1] = std::move(r);
    }

private:
    bool negate;
    int width;
};

// in, en, out. out is in while en is 1, z while it is 0, x while it is
// unknown.
class tristate_component : public sim::component {
public:
    tristate_component(bool negate, int width)
        : negate(negate)
        , width(width)
    {
    }

    void eval(std::span<value> ports, sim::context&) override
    {
        auto& in = ports[0];
        auto en = ports[1].get(0);
        if (en == sim::bit_state::zero) {
            ports[2] = sim::floating(width);
        } else if (en == sim::bit_state::one) {
            value r(width, 0);
            for (int w = 0; w < r.words(); w++) {
                auto x = in.unknown_bits(w);
                r.set_word(w, negate ? ~in.bits(w) & ~x : in.bits(w), x, 0);
            }
            ports[2] = std::move(r);
        } else {
            ports[2] = sim::unknown(width);
        }
    }

private:
    bool negate;
    int width;
};

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
    b.port("out", direction::output, width, {x + grid, 0}, "result");
    width_label(b, width, {x + 2, 2});
}

class gate : public primitive {
public:
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
    std::string category() const override { return "Logic gates"; }

    std::vector<model::param_def> params() const override
    {
        return {
            model::int_param("inputs", "Inputs", 2, 2, 32),
            model::int_param("width", "Bit width", 1, 1, model::max_width, "the gate works on each bit"),
        };
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int inputs = (int)model::get_int(v, "inputs");
        int width = (int)model::get_int(v, "width");

        auto ys = input_ys(inputs);
        int max_y = std::abs(ys.front());

        // the body keeps its size, more inputs extend the back. the input
        // pins stay one grid behind -h.
        int h = std::clamp(max_y + grid, 2 * grid, 3 * grid);
        int back = -h;
        int ext = std::max(h, max_y);

        builder b;

        if (op == op_t::and_op) {
            // a short straight part and a half circle
            back = -h * 3 / 4;
            b.line({back, ext}, {back, -ext});
            b.line({back, h}, {0, h});
            b.line({back, -h}, {0, -h});
            b.arc({0, h}, {h, 0}, {0, -h});
        } else {
            b.bezier({back, h}, {back + h * 6 / 10, 0}, {back, -h});
            b.bezier({back, h}, {h * 3 / 10, h}, {h, 0});
            b.bezier({back, -h}, {h * 3 / 10, -h}, {h, 0});
            if (op == op_t::xor_op) b.bezier({back - 6, h}, {back - 6 + h * 6 / 10, 0}, {back - 6, -h});
            if (ext > h) {
                b.line({back, h}, {back, ext});
                b.line({back, -h}, {back, -ext});
            }
        }

        for (int i = 0; i < inputs; i++) {
            int y = ys[i];
            int x = back;
            // leads end at the curved back of or gates
            if (op != op_t::and_op && std::abs(y) < h) {
                float t = (1.0f - float(y) / h) / 2.0f;
                x = back + (int)std::lround(2.0f * t * (1.0f - t) * h * 6 / 10);
            }
            b.line({-h - grid, y}, {x, y});
            b.port("in" + std::to_string(i), direction::input, width, {-h - grid, y}, "not connected inputs are ignored");
        }

        output(b, h, negate, width);
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<gate_component>(op, negate, (int)model::get_int(v, "inputs"), (int)model::get_int(v, "width"));
    }

private:
    std::string k, n, d;
    op_t op;
    bool negate;
};

class buffer : public primitive {
public:
    explicit buffer(bool negate)
        : negate(negate)
    {
    }

    std::string kind() const override { return negate ? "not" : "buffer"; }
    std::string name() const override { return negate ? "NOT" : "Buffer"; }
    std::string description() const override { return negate ? "inverts every bit" : "passes the input through"; }
    std::string category() const override { return "Logic gates"; }

    std::vector<model::param_def> params() const override
    {
        return {model::int_param("width", "Bit width", 1, 1, model::max_width)};
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int width = (int)model::get_int(v, "width");

        builder b;
        b.polyline({{-grid, grid}, {grid, 0}, {-grid, -grid}, {-grid, grid}});
        b.line({-2 * grid, 0}, {-grid, 0});
        b.port("in", direction::input, width, {-2 * grid, 0}, "input");
        output(b, grid, negate, width);
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<buffer_component>(negate, (int)model::get_int(v, "width"));
    }

private:
    bool negate;
};

// a buffer with an enable input, for buses with several drivers
class tristate : public primitive {
public:
    explicit tristate(bool negate)
        : negate(negate)
    {
    }

    std::string kind() const override { return negate ? "tristate_not" : "tristate"; }
    std::string name() const override { return negate ? "Tri-state NOT" : "Tri-state buffer"; }
    std::string description() const override
    {
        return negate ? "inverts the input while enabled, doesn't drive the output otherwise"
                      : "passes the input while enabled, doesn't drive the output otherwise";
    }
    std::string category() const override { return "Logic gates"; }

    std::vector<model::param_def> params() const override
    {
        return {model::int_param("width", "Bit width", 1, 1, model::max_width)};
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int width = (int)model::get_int(v, "width");

        builder b;
        b.polyline({{-grid, grid}, {grid, 0}, {-grid, -grid}, {-grid, grid}});
        b.line({-2 * grid, 0}, {-grid, 0});
        b.port("in", direction::input, width, {-2 * grid, 0}, "input");
        // enable from below to the middle of the slanted side
        b.line({0, -2 * grid}, {0, -grid / 2});
        b.port("en", direction::input, 1, {0, -2 * grid}, "1 drives the output, 0 leaves it floating (z)");
        output(b, grid, negate, width);
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<tristate_component>(negate, (int)model::get_int(v, "width"));
    }

private:
    bool negate;
};

}

void add_gates(model::primitive_registry& r)
{
    r.add(std::make_unique<gate>("and", "AND", op_t::and_op, false, "1 if all inputs are 1"));
    r.add(std::make_unique<gate>("or", "OR", op_t::or_op, false, "1 if any input is 1"));
    r.add(std::make_unique<gate>("xor", "XOR", op_t::xor_op, false, "1 if an odd number of inputs is 1"));
    r.add(std::make_unique<gate>("nand", "NAND", op_t::and_op, true, "0 if all inputs are 1"));
    r.add(std::make_unique<gate>("nor", "NOR", op_t::or_op, true, "0 if any input is 1"));
    r.add(std::make_unique<gate>("xnor", "XNOR", op_t::xor_op, true, "1 if an even number of inputs is 1"));
    r.add(std::make_unique<buffer>(true));
    r.add(std::make_unique<buffer>(false));
    r.add(std::make_unique<tristate>(false));
    r.add(std::make_unique<tristate>(true));
}

}
