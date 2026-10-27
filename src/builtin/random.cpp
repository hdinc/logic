#include "builder.h"
#include "builtin.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <limits>
#include <optional>

// pseudo-random number generators. both are seeded from a parameter, so a
// simulation gives the same numbers on every run and after a reset.

namespace logic::builtin {

namespace {

using model::direction;
using model::grid;
using sim::value;

// 1, 0, unknown. not connected is def.
std::optional<bool> control(const value& v, bool def)
{
    if (v.fully_floating()) return def;
    auto b = v.get(0);
    if (b == sim::bit_state::one) return true;
    if (b == sim::bit_state::zero) return false;
    return std::nullopt;
}

// clk, en, rst, q. on the rising edge of clk: rst 1 starts over from the
// seed, else en 1 (or not connected) steps to the next number. after a reset
// q already holds the first number. an unknown en or rst at the edge makes q
// unknown until the next step.
class generator_component : public sim::component {
public:
    explicit generator_component(int width)
        : width(width)
    {
    }

    void eval(std::span<value> ports, sim::context&) override
    {
        auto clk = ports[0].get(0);
        bool rising = prev_clk == sim::bit_state::zero && clk == sim::bit_state::one;
        prev_clk = clk;

        if (rising) {
            auto en = control(ports[1], true);
            auto rst = control(ports[2], false);
            if (!rst || (!*rst && !en)) {
                q = sim::unknown(width);
            } else if (*rst) {
                restart();
                q = current();
            } else if (*en) {
                step();
                q = current();
            }
        }
        ports[3] = q;
    }

    void reset() override
    {
        restart();
        q = current();
        prev_clk = sim::bit_state::z;
    }

    void inspect(std::vector<sim::inspect_item>& out) const override { out.push_back({"q", q, {}}); }

protected:
    // back to the seed
    virtual void restart() = 0;
    // to the next number
    virtual void step() = 0;
    virtual value current() const = 0;

    int width;

private:
    value q;
    sim::bit_state prev_clk = sim::bit_state::z;
};

// ---- random: xoshiro256** ----

uint64_t splitmix64(uint64_t& x)
{
    uint64_t z = (x += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

uint64_t rotl(uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

// a number of any width from xoshiro256**, 64 bits per word
class random_component : public generator_component {
public:
    random_component(int width, uint64_t seed)
        : generator_component(width)
        , seed(seed)
    {
        reset();
    }

protected:
    void restart() override
    {
        // the state from the seed, as the authors recommend
        uint64_t x = seed;
        for (auto& w : s)
            w = splitmix64(x);
        step();
    }

    void step() override
    {
        number = value(width, 0);
        for (int w = 0; w < number.words(); w++)
            number.set_word(w, next(), 0, 0);
    }

    value current() const override { return number; }

private:
    uint64_t next()
    {
        uint64_t r = rotl(s[1] * 5, 7) * 9;
        uint64_t t = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = rotl(s[3], 45);
        return r;
    }

    uint64_t seed;
    uint64_t s[4] = {};
    value number;
};

// ---- lfsr ----

// maximal-length taps (bit numbers from 1) for 2 to 64 bits, from Xilinx
// XAPP052. each is a primitive polynomial, so the register goes through all
// 2^n - 1 states that aren't 0.
const std::vector<int>& lfsr_taps(int width)
{
    static const std::vector<std::vector<int>> taps = {
        {}, {}, {2, 1}, {3, 2}, {4, 3}, {5, 3}, {6, 5}, {7, 6}, {8, 6, 5, 4}, {9, 5}, {10, 7}, {11, 9}, {12, 6, 4, 1}, {13, 4, 3, 1},
        {14, 5, 3, 1}, {15, 14}, {16, 15, 13, 4}, {17, 14}, {18, 11}, {19, 6, 2, 1}, {20, 17}, {21, 19}, {22, 21}, {23, 18},
        {24, 23, 22, 17}, {25, 22}, {26, 6, 2, 1}, {27, 5, 2, 1}, {28, 25}, {29, 27}, {30, 6, 4, 1}, {31, 28}, {32, 22, 2, 1},
        {33, 20}, {34, 27, 2, 1}, {35, 33}, {36, 25}, {37, 5, 4, 3, 2, 1}, {38, 6, 5, 1}, {39, 35}, {40, 38, 21, 19}, {41, 38},
        {42, 41, 20, 19}, {43, 42, 38, 37}, {44, 43, 18, 17}, {45, 44, 42, 41}, {46, 45, 26, 25}, {47, 42}, {48, 47, 21, 20},
        {49, 40}, {50, 49, 24, 23}, {51, 50, 36, 35}, {52, 49}, {53, 52, 38, 37}, {54, 53, 18, 17}, {55, 31}, {56, 55, 35, 34},
        {57, 50}, {58, 39}, {59, 58, 38, 37}, {60, 59}, {61, 60, 46, 45}, {62, 61, 6, 5}, {63, 62}, {64, 63, 61, 60},
    };
    return taps[std::clamp(width, 2, 64)];
}

uint64_t default_taps(int width)
{
    uint64_t m = 0;
    for (int t : lfsr_taps(width))
        m |= uint64_t(1) << (t - 1);
    return m;
}

// the taps of the "taps" parameter as a mask of bits: bit numbers from 1
// separated by spaces or commas (e.g. "8 6 5 4"), or a mask in hex (e.g.
// "0xB8"). empty: the maximal-length taps. an error for anything else.
std::optional<uint64_t> parse_taps(const std::string& text, int width, std::string& error)
{
    std::vector<std::string> tokens;
    std::string cur;
    for (char ch : text + " ") {
        if (ch == ' ' || ch == ',' || ch == '\t') {
            if (!cur.empty()) tokens.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    if (tokens.empty()) return default_taps(width);

    auto all_of = [](const std::string& t, size_t from, auto pred) {
        return t.size() > from && std::all_of(t.begin() + from, t.end(), [&](char ch) { return pred((unsigned char)ch); });
    };
    if (tokens.size() == 1 && tokens[0].size() > 2 && tokens[0][0] == '0' && (tokens[0][1] == 'x' || tokens[0][1] == 'X')) {
        auto& t = tokens[0];
        if (!all_of(t, 2, [](unsigned char ch) { return std::isxdigit(ch) != 0; }) || t.size() > 18) {
            error = "taps: " + t + " isn't a hex mask of up to 64 bits";
            return std::nullopt;
        }
        uint64_t m = std::stoull(t.substr(2), nullptr, 16);
        if (m & ~sim::mask(width)) {
            error = "taps: mask " + t + " has bits above the width";
            return std::nullopt;
        }
        if (!m) {
            error = "taps: the mask has no bits";
            return std::nullopt;
        }
        return m;
    }

    uint64_t m = 0;
    for (auto& t : tokens) {
        if (!all_of(t, 0, [](unsigned char ch) { return std::isdigit(ch) != 0; }) || t.size() > 3) {
            error = "taps: " + t + " isn't a bit number";
            return std::nullopt;
        }
        int n = std::stoi(t);
        if (n < 1 || n > width) {
            error = "taps: bit " + t + " isn't between 1 and the width " + std::to_string(width);
            return std::nullopt;
        }
        m |= uint64_t(1) << (n - 1);
    }
    return m;
}

// fibonacci form: shifts left, the new bit 0 is the xor of the taps
class lfsr_component : public generator_component {
public:
    lfsr_component(int width, uint64_t seed, uint64_t taps)
        : generator_component(width)
        , mask(sim::mask(width))
        , feedback(taps & mask)
    {
        // 0 would stay 0
        this->seed = (seed & mask) ? seed & mask : 1;
        reset();
    }

protected:
    void restart() override { state = seed; }

    void step() override
    {
        uint64_t bit = std::popcount(state & feedback) & 1;
        state = ((state << 1) | bit) & mask;
    }

    value current() const override { return sim::known(state, width); }

public:
    void inspect(std::vector<sim::inspect_item>& out) const override
    {
        generator_component::inspect(out);
        out.push_back({"taps", sim::known(feedback, width), {}});
    }

private:
    uint64_t mask;
    uint64_t feedback;
    uint64_t seed;
    uint64_t state = 0;
};

// ---- primitives ----

model::cell_interface generator_symbol(const std::string& title, int width)
{
    builder b;
    b.box(title,
        {
            {"clk", direction::input, 1, "steps on the rising edge", ">"},
            {"en", direction::input, 1, "1 steps, 0 holds. steps if not connected"},
            {"rst", direction::input, 1, "1 starts over from the seed on the clock edge"},
        },
        {
            {"q", direction::output, width, "the current number, the first one after reset"},
        });
    return b.finish();
}

model::param_def seed_param()
{
    return model::int_param("seed", "Seed", 1, std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max(),
        "the same seed gives the same numbers on every run");
}

class random_primitive : public primitive {
public:
    std::string kind() const override { return "random"; }
    std::string name() const override { return "Random"; }
    std::string category() const override { return "Logic sources"; }
    std::string description() const override { return "a new pseudo-random number on each rising clock edge (xoshiro256**)"; }

    std::vector<model::param_def> params() const override
    {
        return {model::int_param("width", "Bit width", 8, 1, model::max_width), seed_param()};
    }

    model::cell_interface build(const model::param_values& v) const override { return generator_symbol("rnd", (int)model::get_int(v, "width")); }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<random_component>((int)model::get_int(v, "width"), (uint64_t)model::get_int(v, "seed"));
    }
};

class lfsr_primitive : public primitive {
public:
    std::string kind() const override { return "lfsr"; }
    std::string name() const override { return "LFSR"; }
    std::string category() const override { return "Logic sources"; }
    std::string description() const override
    {
        return "a linear feedback shift register: shifts left by one on each rising clock edge, bit 0 gets the xor of its taps. "
               "with the default taps it goes through all 2^n - 1 values but 0";
    }

    std::vector<model::param_def> params() const override
    {
        auto seed = seed_param();
        seed.description = "the first value, bits above the width are ignored. 0 is taken as 1";
        return {
            model::int_param("width", "Bit width", 8, 2, 64),
            seed,
            model::text_param("taps", "Taps", "",
                "bit numbers from 1 that feed back, e.g. 8 6 5 4, or a mask in hex, e.g. 0xB8. "
                "empty: maximal-length taps for the width, which go through all 2^n - 1 values"),
        };
    }

    std::string check(const model::param_values& v) const override
    {
        std::string error;
        if (!parse_taps(model::get_string(v, "taps"), (int)model::get_int(v, "width"), error)) return error + ", the maximal-length taps are used";
        return {};
    }

    model::cell_interface build(const model::param_values& v) const override { return generator_symbol("lfsr", (int)model::get_int(v, "width")); }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        int width = (int)model::get_int(v, "width");
        std::string error;
        auto taps = parse_taps(model::get_string(v, "taps"), width, error);
        return std::make_unique<lfsr_component>(width, (uint64_t)model::get_int(v, "seed"), taps.value_or(default_taps(width)));
    }
};

}

void add_random(model::primitive_registry& r)
{
    r.add(std::make_unique<random_primitive>());
    r.add(std::make_unique<lfsr_primitive>());
}

}
