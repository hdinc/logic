#include "../sim/arith.h"
#include "builder.h"
#include "builtin.h"

#include <algorithm>
#include <bit>
#include <climits>
#include <optional>

namespace logic::builtin {

namespace {

using model::direction;
using model::grid;
using sim::value;
using pin = builder::box_pin;

// bits to count from 0 to n - 1, at least 1
int index_bits(int n)
{
    return std::max(1, (int)std::bit_width((unsigned)std::max(n - 1, 1)));
}

// an optional control input that isn't connected (all z) counts as `def`.
// null if it's unknown.
std::optional<bool> control(const value& v, bool def)
{
    if (v.fully_floating()) return def;
    auto b = v.get(0);
    if (b == sim::bit_state::one) return true;
    if (b == sim::bit_state::zero) return false;
    return std::nullopt;
}

bool all_known(std::span<value> ports, std::initializer_list<int> which)
{
    for (int k : which)
        if (!ports[k].fully_known()) return false;
    return true;
}

// ---- adder / subtractor ----

// a, b, [sub], [cin], y, [cout]
class addsub_component : public sim::component {
public:
    addsub_component(int width, int mode, bool cin, bool cout)
        : width(width)
        , mode(mode)
        , has_cin(cin)
        , has_cout(cout)
    {
    }

    void eval(std::span<value> ports, sim::context&) override
    {
        int k = 2;
        std::optional<bool> subtract = mode == 1;
        if (mode == 2) subtract = control(ports[k++], false);
        std::optional<bool> carry = false;
        if (has_cin) carry = control(ports[k++], false);
        int y = k;

        if (!subtract || !carry || !all_known(ports, {0, 1})) {
            ports[y] = sim::unknown(width);
            if (has_cout) ports[y + 1] = sim::unknown(1);
            return;
        }
        auto r = *subtract ? sim::sub(ports[0], ports[1], *carry) : sim::add(ports[0], ports[1], *carry);
        ports[y] = r.v;
        if (has_cout) ports[y + 1] = sim::known(r.carry, 1);
    }

private:
    int width;
    int mode;
    bool has_cin;
    bool has_cout;
};

class addsub : public primitive {
public:
    std::string kind() const override { return "addsub"; }
    std::string name() const override { return "Adder/subtractor"; }
    std::string category() const override { return "Arithmetic"; }
    std::string description() const override { return "adds or subtracts two numbers, with carry (borrow) in and out"; }

    std::vector<model::param_def> params() const override
    {
        return {
            model::int_param("width", "Bit width", 8, 1, model::max_width),
            model::choice_param("mode", "Mode", {"add", "subtract", "input"}, "add", "input: a sub pin chooses, 1 subtracts"),
            model::bool_param("carry_in", "Carry in", false, "a carry (borrow when subtracting) input"),
            model::bool_param("carry_out", "Carry out", true, "a carry (borrow when subtracting) output"),
        };
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int width = (int)model::get_int(v, "width");
        auto mode = model::get_string(v, "mode");
        std::vector<pin> left{{"a", direction::input, width, "first operand"}, {"b", direction::input, width, "second operand"}};
        if (mode == "input") left.push_back({"sub", direction::input, 1, "1 subtracts b, 0 adds it"});
        if (model::get_bool(v, "carry_in")) left.push_back({"cin", direction::input, 1, "carry in, borrow in when subtracting, 0 if not connected"});
        std::vector<pin> right{{"y", direction::output, width, mode == "add" ? "a + b" : mode == "subtract" ? "a - b"
                                                                                                            : "a + b or a - b"}};
        if (model::get_bool(v, "carry_out")) right.push_back({"cout", direction::output, 1, "carry out, borrow out when subtracting"});

        builder b;
        b.box(mode == "add" ? "+" : mode == "subtract" ? "-"
                                                       : "+/-",
              left, right);
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        auto mode = model::get_string(v, "mode");
        return std::make_unique<addsub_component>((int)model::get_int(v, "width"), mode == "add" ? 0 : mode == "subtract" ? 1
                                                                                                                          : 2,
                                                  model::get_bool(v, "carry_in"), model::get_bool(v, "carry_out"));
    }
};

// ---- multiplier ----

// a, b, y
class multiplier_component : public sim::component {
public:
    multiplier_component(int width, bool is_signed, bool full)
        : width(width)
        , is_signed(is_signed)
        , full(full)
    {
    }

    void eval(std::span<value> ports, sim::context&) override
    {
        int out = full ? 2 * width : width;
        if (!all_known(ports, {0, 1})) {
            ports[2] = sim::unknown(out);
            return;
        }
        ports[2] = sim::mul(ports[0], ports[1], is_signed).slice(0, out);
    }

private:
    int width;
    bool is_signed;
    bool full;
};

class multiplier : public primitive {
public:
    std::string kind() const override { return "multiplier"; }
    std::string name() const override { return "Multiplier"; }
    std::string category() const override { return "Arithmetic"; }
    std::string description() const override { return "multiplies two numbers"; }

    std::vector<model::param_def> params() const override
    {
        return {
            model::int_param("width", "Bit width", 8, 1, model::max_width / 2),
            model::bool_param("signed", "Signed", false, "two's complement operands"),
            model::choice_param("output", "Output", {"low", "full"}, "low", "low: the low half of the product, full: all of it, twice as wide"),
        };
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int width = (int)model::get_int(v, "width");
        bool full = model::get_string(v, "output") == "full";
        builder b;
        b.box("x", {{"a", direction::input, width, "first factor"}, {"b", direction::input, width, "second factor"}}, {{"y", direction::output, full ? 2 * width : width, full ? "a * b, twice as wide" : "the low half of a * b"}});
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<multiplier_component>((int)model::get_int(v, "width"), model::get_bool(v, "signed"), model::get_string(v, "output") == "full");
    }
};

// ---- divider ----

// a, b, q, [r]
class divider_component : public sim::component {
public:
    divider_component(int width, bool is_signed, bool rem)
        : width(width)
        , is_signed(is_signed)
        , rem(rem)
    {
    }

    void eval(std::span<value> ports, sim::context&) override
    {
        if (!all_known(ports, {0, 1}) || sim::is_zero(ports[1])) {
            ports[2] = sim::unknown(width);
            if (rem) ports[3] = sim::unknown(width);
            return;
        }
        auto r = sim::divmod(ports[0], ports[1], is_signed);
        ports[2] = r.q;
        if (rem) ports[3] = r.r;
    }

private:
    int width;
    bool is_signed;
    bool rem;
};

class divider : public primitive {
public:
    std::string kind() const override { return "divider"; }
    std::string name() const override { return "Divider"; }
    std::string category() const override { return "Arithmetic"; }
    std::string description() const override { return "divides two numbers, rounding towards 0. dividing by 0 gives x"; }

    std::vector<model::param_def> params() const override
    {
        return {
            model::int_param("width", "Bit width", 8, 1, model::max_width),
            model::bool_param("signed", "Signed", false, "two's complement operands, the remainder has the sign of a"),
            model::bool_param("remainder", "Remainder", true, "a remainder output"),
        };
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int width = (int)model::get_int(v, "width");
        std::vector<pin> right{{"q", direction::output, width, "a / b"}};
        if (model::get_bool(v, "remainder")) right.push_back({"r", direction::output, width, "the remainder of a / b"});
        builder b;
        b.box("/", {{"a", direction::input, width, "dividend"}, {"b", direction::input, width, "divisor"}}, right);
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<divider_component>((int)model::get_int(v, "width"), model::get_bool(v, "signed"), model::get_bool(v, "remainder"));
    }
};

// ---- negator ----

class negator_component : public sim::component {
public:
    explicit negator_component(int width)
        : width(width)
    {
    }

    void eval(std::span<value> ports, sim::context&) override
    {
        ports[1] = ports[0].fully_known() ? sim::negate(ports[0]) : sim::unknown(width);
    }

private:
    int width;
};

class negator : public primitive {
public:
    std::string kind() const override { return "negator"; }
    std::string name() const override { return "Negator"; }
    std::string category() const override { return "Arithmetic"; }
    std::string description() const override { return "the two's complement: -in"; }

    std::vector<model::param_def> params() const override { return {model::int_param("width", "Bit width", 8, 1, model::max_width)}; }

    model::cell_interface build(const model::param_values& v) const override
    {
        int width = (int)model::get_int(v, "width");
        builder b;
        b.box("-x", {{"in", direction::input, width, "the number"}}, {{"out", direction::output, width, "-in"}});
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<negator_component>((int)model::get_int(v, "width"));
    }
};

// ---- comparator ----

// a, b, lt, eq, gt
class comparator_component : public sim::component {
public:
    explicit comparator_component(bool is_signed)
        : is_signed(is_signed)
    {
    }

    void eval(std::span<value> ports, sim::context&) override
    {
        if (!all_known(ports, {0, 1})) {
            for (int k = 2; k < 5; k++)
                ports[k] = sim::unknown(1);
            return;
        }
        int c = sim::compare(ports[0], ports[1], is_signed);
        ports[2] = sim::known(c < 0, 1);
        ports[3] = sim::known(c == 0, 1);
        ports[4] = sim::known(c > 0, 1);
    }

private:
    bool is_signed;
};

class comparator : public primitive {
public:
    std::string kind() const override { return "comparator"; }
    std::string name() const override { return "Comparator"; }
    std::string category() const override { return "Arithmetic"; }
    std::string description() const override { return "compares two numbers"; }

    std::vector<model::param_def> params() const override
    {
        return {
            model::int_param("width", "Bit width", 8, 1, model::max_width),
            model::bool_param("signed", "Signed", false, "two's complement operands"),
        };
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int width = (int)model::get_int(v, "width");
        builder b;
        b.box("cmp", {{"a", direction::input, width, "first operand"}, {"b", direction::input, width, "second operand"}}, {{"lt", direction::output, 1, "1 if a < b", "<"}, {"eq", direction::output, 1, "1 if a = b", "="}, {"gt", direction::output, 1, "1 if a > b", ">"}});
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<comparator_component>(model::get_bool(v, "signed"));
    }
};

// ---- shifter ----

enum class shift_op {
    shl,
    shr,
    sar,
    rol,
    ror
};

const std::vector<std::string> shift_names = {"shift left", "shift right", "arithmetic right", "rotate left", "rotate right"};

// in, amount, out
class shifter_component : public sim::component {
public:
    shifter_component(int width, shift_op op)
        : width(width)
        , op(op)
    {
    }

    void eval(std::span<value> ports, sim::context&) override
    {
        if (!all_known(ports, {0, 1})) {
            ports[2] = sim::unknown(width);
            return;
        }
        // the amount fits an int: it has at most index_bits(width) bits
        int n = (int)ports[1].to_u64();
        auto& in = ports[0];
        switch (op) {
        case shift_op::shl: ports[2] = sim::shl(in, n); break;
        case shift_op::shr: ports[2] = sim::shr(in, n); break;
        case shift_op::sar: ports[2] = sim::sar(in, n); break;
        case shift_op::rol: ports[2] = sim::rotl(in, n); break;
        case shift_op::ror: ports[2] = sim::rotr(in, n); break;
        }
    }

private:
    int width;
    shift_op op;
};

class shifter : public primitive {
public:
    std::string kind() const override { return "shifter"; }
    std::string name() const override { return "Shifter"; }
    std::string category() const override { return "Arithmetic"; }
    std::string description() const override { return "shifts or rotates by a number of bits (a barrel shifter)"; }

    std::vector<model::param_def> params() const override
    {
        return {
            model::int_param("width", "Bit width", 8, 2, model::max_width),
            model::choice_param("operation", "Operation", shift_names, "shift left", "shifts fill with 0, the arithmetic right shift with the top bit; rotations wrap around"),
        };
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int width = (int)model::get_int(v, "width");
        auto op = op_of(v);
        const char* titles[] = {"<<", ">>", ">>>", "rol", "ror"};
        builder b;
        b.box(titles[(int)op], {{"in", direction::input, width, "the bits to shift"}, {"amount", direction::input, index_bits(width), "by how many bits", "n"}}, {{"out", direction::output, width, model::get_string(v, "operation") + " in by amount"}});
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<shifter_component>((int)model::get_int(v, "width"), op_of(v));
    }

private:
    static shift_op op_of(const model::param_values& v)
    {
        auto s = model::get_string(v, "operation");
        for (size_t k = 0; k < shift_names.size(); k++)
            if (shift_names[k] == s) return (shift_op)k;
        return shift_op::shl;
    }
};

// ---- bit extender ----

class extender_component : public sim::component {
public:
    extender_component(int out, sim::extend_mode mode)
        : out(out)
        , mode(mode)
    {
    }

    void eval(std::span<value> ports, sim::context&) override
    {
        auto& in = ports[0];
        // the bits are copied with their state, floating ones become x
        value r = sim::extend(in, out, mode);
        for (int w = 0; w < r.words(); w++)
            r.set_word(w, r.bits(w), r.unknown_bits(w), 0);
        // the new bits follow an unknown sign bit
        auto top = in.get(in.width() - 1);
        if (mode == sim::extend_mode::sign && top != sim::bit_state::zero && top != sim::bit_state::one)
            for (int i = in.width(); i < out; i++)
                r.set(i, sim::bit_state::x);
        ports[1] = std::move(r);
    }

private:
    int out;
    sim::extend_mode mode;
};

class extender : public primitive {
public:
    std::string kind() const override { return "extender"; }
    std::string name() const override { return "Bit extender"; }
    std::string category() const override { return "Arithmetic"; }
    std::string description() const override { return "makes a number wider (zero, sign or one extension) or narrower (its low bits)"; }

    std::vector<model::param_def> params() const override
    {
        return {
            model::int_param("in_width", "Input width", 8, 1, model::max_width),
            model::int_param("out_width", "Output width", 16, 1, model::max_width),
            model::choice_param("mode", "Extension", {"zero", "sign", "one"}, "sign", "what the new top bits are"),
        };
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int in = (int)model::get_int(v, "in_width");
        int out = (int)model::get_int(v, "out_width");
        auto mode = model::get_string(v, "mode");
        builder b;
        b.box(mode == "sign" ? "sext" : mode == "zero" ? "zext"
                                                       : "1ext",
              {{"in", direction::input, in, "the number"}}, {{"out", direction::output, out, out >= in ? "in with " + mode + " extension" : "the low bits of in"}});
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        auto mode = model::get_string(v, "mode");
        return std::make_unique<extender_component>((int)model::get_int(v, "out_width"), mode == "sign" ? sim::extend_mode::sign : mode == "zero" ? sim::extend_mode::zero
                                                                                                                                                  : sim::extend_mode::one);
    }
};

// ---- counter ----

// clk, en, load, d, rst, q, carry. on the rising edge of clk: rst clears,
// else load takes d, else en counts. not connected en counts, load and rst
// are off.
class counter_component : public sim::component {
public:
    counter_component(int width, value max, bool down, bool wrap)
        : width(width)
        , max(std::move(max))
        , down(down)
        , wrap(wrap)
    {
        reset();
    }

    void eval(std::span<value> ports, sim::context&) override
    {
        auto clk = ports[0].get(0);
        bool rising = prev_clk == sim::bit_state::zero && clk == sim::bit_state::one;
        prev_clk = clk;
        auto en = control(ports[1], true);

        if (rising) {
            auto rst = control(ports[4], false);
            auto load = control(ports[2], false);
            // an unknown control on the edge makes the count unknown
            if (!rst || (!*rst && !load) || (!*rst && !*load && !en))
                q = sim::unknown(width);
            else if (*rst)
                q = value(width, 0);
            else if (*load)
                q = take(ports[3]);
            else if (*en)
                q = q.fully_known() ? next() : sim::unknown(width);
        }

        ports[5] = q;
        // at the end while counting
        if (!en || !q.fully_known())
            ports[6] = sim::unknown(1);
        else
            ports[6] = sim::known(*en && at_end(), 1);
    }

    void reset() override
    {
        q = value(width, 0);
        prev_clk = sim::bit_state::z;
    }

    void inspect(std::vector<sim::inspect_item>& out) const override { out.push_back({"q", q, {}}); }

    // q's words (bits, x) and the clock
    bool save_state(std::vector<uint8_t>& out) const override
    {
        out.clear();
        auto put = [&](uint64_t v) {
            for (int k = 0; k < 8; k++)
                out.push_back(uint8_t(v >> (8 * k)));
        };
        for (int w = 0; w < q.words(); w++) {
            put(q.bits(w));
            put(q.x(w));
        }
        out.push_back((uint8_t)prev_clk);
        return true;
    }

    bool load_state(std::span<const uint8_t> in) override
    {
        if (in.size() != size_t(16 * q.words() + 1)) return false;
        auto get = [&](size_t at) {
            uint64_t v = 0;
            for (int k = 0; k < 8; k++)
                v |= uint64_t(in[at + k]) << (8 * k);
            return v;
        };
        for (int w = 0; w < q.words(); w++)
            q.set_word(w, get(16 * w), get(16 * w + 8), 0);
        prev_clk = (sim::bit_state)(in.back() & 3);
        return true;
    }

private:
    bool at_end() const { return down ? sim::is_zero(q) : q == max; }

    // d with its unknown (and floating) bits as x
    value take(const value& d) const
    {
        value r(width, 0);
        for (int w = 0; w < r.words(); w++)
            r.set_word(w, d.bits(w), d.unknown_bits(w), 0);
        return r;
    }

    value next() const
    {
        if (at_end()) return wrap ? (down ? max : value(width, 0)) : q;
        return down ? sim::sub(q, value(width, 1)).v : sim::add(q, value(width, 1)).v;
    }

    int width;
    value max;
    bool down;
    bool wrap;
    value q;
    sim::bit_state prev_clk = sim::bit_state::z;
};

class counter : public primitive {
public:
    std::string kind() const override { return "counter"; }
    std::string name() const override { return "Counter"; }
    std::string category() const override { return "Memory"; }
    std::string description() const override { return "counts rising clock edges up or down, with load and reset"; }

    std::vector<model::param_def> params() const override
    {
        return {
            model::int_param("width", "Bit width", 8, 1, model::max_width),
            model::int_param("max", "Maximum", 0, 0, INT64_MAX, "the value where it wraps or stays, 0 for all ones"),
            model::choice_param("direction", "Direction", {"up", "down"}, "up"),
            model::choice_param("at_end", "At the end", {"wrap", "stay"}, "wrap", "wrap: up goes to 0 after the maximum, down to the maximum after 0"),
        };
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int width = (int)model::get_int(v, "width");
        bool down = model::get_string(v, "direction") == "down";
        builder b;
        b.box(down ? "ctr -" : "ctr +", {
                                            {"clk", direction::input, 1, "counts on the rising edge", ">"},
                                            {"en", direction::input, 1, "1 counts, 0 holds. counts if not connected"},
                                            {"load", direction::input, 1, "1 takes d on the clock edge instead of counting"},
                                            {"d", direction::input, width, "the value to load"},
                                            {"rst", direction::input, 1, "1 clears to 0 on the clock edge, before load"},
                                        },
              {
                  {"q", direction::output, width, "the count, 0 after reset"},
                  {"carry", direction::output, 1, down ? "1 at 0 while counting" : "1 at the maximum while counting"},
              });
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        int width = (int)model::get_int(v, "width");
        auto m = model::get_int(v, "max");
        // all ones, or the maximum if it fits the width
        value max = sim::negate(value(width, 1));
        if (m > 0 && (width >= 64 || (uint64_t)m <= sim::mask(width))) max = sim::known((uint64_t)m, width);
        return std::make_unique<counter_component>(width, max, model::get_string(v, "direction") == "down", model::get_string(v, "at_end") == "wrap");
    }
};

}

void add_arith(model::primitive_registry& r)
{
    r.add(std::make_unique<addsub>());
    r.add(std::make_unique<multiplier>());
    r.add(std::make_unique<divider>());
    r.add(std::make_unique<negator>());
    r.add(std::make_unique<comparator>());
    r.add(std::make_unique<shifter>());
    r.add(std::make_unique<extender>());
    r.add(std::make_unique<counter>());
}

}
