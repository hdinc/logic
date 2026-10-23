#include "../sim/arith.h"
#include "builder.h"
#include "builtin.h"

#include <algorithm>
#include <bit>
#include <optional>

namespace logic::builtin {

namespace {

using model::direction;
using sim::value;
using pin = builder::box_pin;

int index_bits(int n)
{
    return std::max(1, (int)std::bit_width((unsigned)std::max(n - 1, 1)));
}

// a not connected enable is on. null if it's unknown.
std::optional<bool> enable(const value& v)
{
    if (v.fully_floating()) return true;
    auto b = v.get(0);
    if (b == sim::bit_state::one) return true;
    if (b == sim::bit_state::zero) return false;
    return std::nullopt;
}

// the select as a number, null if it has unknown bits
std::optional<int> index_of(const value& v)
{
    if (!v.fully_known()) return std::nullopt;
    return (int)v.to_u64();
}

// ---- mux ----

// in0..inN-1, sel, [en], out
class mux_component : public sim::component {
public:
    mux_component(int inputs, int width, bool has_en)
        : inputs(inputs)
        , width(width)
        , has_en(has_en)
    {
    }

    void eval(std::span<value> ports, sim::context&) override
    {
        auto sel = index_of(ports[inputs]);
        auto en = has_en ? enable(ports[inputs + 1]) : true;
        auto& out = ports[inputs + (has_en ? 2 : 1)];
        if (!en || (*en && !sel))
            out = sim::unknown(width);
        else if (!*en)
            out = value(width, 0);
        else
            out = ports[*sel];
    }

private:
    int inputs;
    int width;
    bool has_en;
};

std::vector<model::param_def> select_params(int max_bits, bool width)
{
    std::vector<model::param_def> r{model::int_param("select", "Select bits", 1, 1, max_bits)};
    if (width) r.push_back(model::int_param("width", "Bit width", 1, 1, model::max_width));
    r.push_back(model::bool_param("enable", "Enable", false, "an enable input, 0 makes the outputs 0. on if not connected"));
    return r;
}

class mux : public primitive {
public:
    std::string kind() const override { return "mux"; }
    std::string name() const override { return "Multiplexer"; }
    std::string category() const override { return "Plexers"; }
    std::string description() const override { return "passes the input the select chooses"; }

    std::vector<model::param_def> params() const override { return select_params(6, true); }

    model::cell_interface build(const model::param_values& v) const override
    {
        int bits = (int)model::get_int(v, "select");
        int width = (int)model::get_int(v, "width");
        std::vector<pin> left;
        for (int k = 0; k < (1 << bits); k++)
            left.push_back({"in" + std::to_string(k), direction::input, width, "passed when sel is " + std::to_string(k), std::to_string(k)});
        std::vector<pin> bottom{{"sel", direction::input, bits, "which input to pass"}};
        if (model::get_bool(v, "enable")) bottom.push_back({"en", direction::input, 1, "0 makes out 0"});
        builder b;
        b.box("MUX", left, {{"out", direction::output, width, "the chosen input"}}, bottom);
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<mux_component>(1 << model::get_int(v, "select"), (int)model::get_int(v, "width"), model::get_bool(v, "enable"));
    }
};

// ---- demux ----

// in, sel, [en], out0..outN-1
class demux_component : public sim::component {
public:
    demux_component(int outputs, int width, bool has_en)
        : outputs(outputs)
        , width(width)
        , has_en(has_en)
    {
    }

    void eval(std::span<value> ports, sim::context&) override
    {
        auto sel = index_of(ports[1]);
        auto en = has_en ? enable(ports[2]) : true;
        int first = has_en ? 3 : 2;
        for (int k = 0; k < outputs; k++) {
            auto& out = ports[first + k];
            if (!en || (*en && !sel))
                out = sim::unknown(width);
            else if (*en && *sel == k)
                out = ports[0];
            else
                out = value(width, 0);
        }
    }

private:
    int outputs;
    int width;
    bool has_en;
};

class demux : public primitive {
public:
    std::string kind() const override { return "demux"; }
    std::string name() const override { return "Demultiplexer"; }
    std::string category() const override { return "Plexers"; }
    std::string description() const override { return "passes the input to the output the select chooses, the others are 0"; }

    std::vector<model::param_def> params() const override { return select_params(6, true); }

    model::cell_interface build(const model::param_values& v) const override
    {
        int bits = (int)model::get_int(v, "select");
        int width = (int)model::get_int(v, "width");
        std::vector<pin> right;
        for (int k = 0; k < (1 << bits); k++)
            right.push_back({"out" + std::to_string(k), direction::output, width, "in when sel is " + std::to_string(k) + ", else 0", std::to_string(k)});
        std::vector<pin> bottom{{"sel", direction::input, bits, "which output gets in"}};
        if (model::get_bool(v, "enable")) bottom.push_back({"en", direction::input, 1, "0 makes all outputs 0"});
        builder b;
        b.box("DMX", {{"in", direction::input, width, "the value to pass"}}, right, bottom);
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<demux_component>(1 << model::get_int(v, "select"), (int)model::get_int(v, "width"), model::get_bool(v, "enable"));
    }
};

// ---- decoder ----

// sel, [en], out (one bit per value of sel)
class decoder_component : public sim::component {
public:
    decoder_component(int outputs, bool has_en)
        : outputs(outputs)
        , has_en(has_en)
    {
    }

    void eval(std::span<value> ports, sim::context&) override
    {
        auto sel = index_of(ports[0]);
        auto en = has_en ? enable(ports[1]) : true;
        auto& out = ports[has_en ? 2 : 1];
        if (!en || (*en && !sel)) {
            out = sim::unknown(outputs);
            return;
        }
        out = value(outputs, 0);
        if (*en) out.set(*sel, sim::bit_state::one);
    }

private:
    int outputs;
    bool has_en;
};

class decoder : public primitive {
public:
    std::string kind() const override { return "decoder"; }
    std::string name() const override { return "Decoder"; }
    std::string category() const override { return "Plexers"; }
    std::string description() const override { return "sets the output bit the select chooses (one-hot)"; }

    std::vector<model::param_def> params() const override { return select_params(12, false); }

    model::cell_interface build(const model::param_values& v) const override
    {
        int bits = (int)model::get_int(v, "select");
        std::vector<pin> bottom;
        if (model::get_bool(v, "enable")) bottom.push_back({"en", direction::input, 1, "0 makes all bits 0"});
        builder b;
        b.box("DEC", {{"sel", direction::input, bits, "which bit to set"}}, {{"out", direction::output, 1 << bits, "bit sel is 1, the others 0"}}, bottom);
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<decoder_component>(1 << model::get_int(v, "select"), model::get_bool(v, "enable"));
    }
};

// ---- priority encoder ----

// in, index, valid
class priority_component : public sim::component {
public:
    explicit priority_component(int bits)
        : bits(bits)
    {
    }

    void eval(std::span<value> ports, sim::context&) override
    {
        auto& in = ports[0];
        // from the top: the first 1 wins, an unknown bit before it makes it
        // unknown
        for (int i = in.width() - 1; i >= 0; i--) {
            auto b = in.get(i);
            if (b == sim::bit_state::zero) continue;
            if (b == sim::bit_state::one) {
                ports[1] = sim::known(i, bits);
                ports[2] = sim::known(1, 1);
            } else {
                ports[1] = sim::unknown(bits);
                ports[2] = sim::unknown(1);
            }
            return;
        }
        ports[1] = value(bits, 0);
        ports[2] = sim::known(0, 1);
    }

private:
    int bits;
};

class priority_encoder : public primitive {
public:
    std::string kind() const override { return "priority"; }
    std::string name() const override { return "Priority encoder"; }
    std::string category() const override { return "Plexers"; }
    std::string description() const override { return "the index of the highest 1 bit"; }

    std::vector<model::param_def> params() const override { return {model::int_param("inputs", "Input bits", 8, 2, model::max_width)}; }

    model::cell_interface build(const model::param_values& v) const override
    {
        int n = (int)model::get_int(v, "inputs");
        builder b;
        b.box("PRI", {{"in", direction::input, n, "the bits, the highest 1 wins"}}, {{"index", direction::output, index_bits(n), "the index of the highest 1, 0 if there is none"}, {"valid", direction::output, 1, "1 if a bit is 1"}});
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<priority_component>(index_bits((int)model::get_int(v, "inputs")));
    }
};

// ---- bit counter ----

class popcount_component : public sim::component {
public:
    explicit popcount_component(int bits)
        : bits(bits)
    {
    }

    void eval(std::span<value> ports, sim::context&) override
    {
        ports[1] = ports[0].fully_known() ? sim::known(sim::popcount(ports[0]), bits) : sim::unknown(bits);
    }

private:
    int bits;
};

// bits for numbers 0..n
int count_bits(int n)
{
    return std::max(1, (int)std::bit_width((unsigned)n));
}

class bit_counter : public primitive {
public:
    std::string kind() const override { return "popcount"; }
    std::string name() const override { return "Bit counter"; }
    std::string category() const override { return "Plexers"; }
    std::string description() const override { return "the number of 1 bits"; }

    std::vector<model::param_def> params() const override { return {model::int_param("width", "Bit width", 8, 1, model::max_width)}; }

    model::cell_interface build(const model::param_values& v) const override
    {
        int width = (int)model::get_int(v, "width");
        builder b;
        b.box("#1", {{"in", direction::input, width, "the bits to count"}}, {{"count", direction::output, count_bits(width), "how many bits are 1"}});
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<popcount_component>(count_bits((int)model::get_int(v, "width")));
    }
};

// ---- bit selector ----

// in, sel, out: bits [sel * out, (sel + 1) * out) of in, 0 above it
class bitselect_component : public sim::component {
public:
    bitselect_component(int width, int out)
        : width(width)
        , out(out)
    {
    }

    void eval(std::span<value> ports, sim::context&) override
    {
        auto sel = index_of(ports[1]);
        if (!sel) {
            ports[2] = sim::unknown(out);
            return;
        }
        int lo = *sel * out;
        value r(out, 0);
        if (lo < width) r.insert(0, ports[0].slice(lo, std::min(out, width - lo)));
        ports[2] = std::move(r);
    }

private:
    int width;
    int out;
};

class bit_selector : public primitive {
public:
    std::string kind() const override { return "bitselect"; }
    std::string name() const override { return "Bit selector"; }
    std::string category() const override { return "Plexers"; }
    std::string description() const override { return "picks a group of bits of a bus by an index"; }

    std::vector<model::param_def> params() const override
    {
        return {
            model::int_param("width", "Bit width", 8, 1, model::max_width),
            model::int_param("out", "Output bits", 1, 1, model::max_width, "bits per group, sel = 1 picks the second group"),
        };
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int width = (int)model::get_int(v, "width");
        int out = (int)model::get_int(v, "out");
        int groups = (width + out - 1) / out;
        builder b;
        b.box("SEL", {{"in", direction::input, width, "the bus"}, {"sel", direction::input, index_bits(groups), "which group of bits"}}, {{"out", direction::output, out, "bits sel * out and up of in, 0 beyond the bus"}});
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<bitselect_component>((int)model::get_int(v, "width"), (int)model::get_int(v, "out"));
    }
};

}

void add_plexers(model::primitive_registry& r)
{
    r.add(std::make_unique<mux>());
    r.add(std::make_unique<demux>());
    r.add(std::make_unique<decoder>());
    r.add(std::make_unique<priority_encoder>());
    r.add(std::make_unique<bit_counter>());
    r.add(std::make_unique<bit_selector>());
}

}
