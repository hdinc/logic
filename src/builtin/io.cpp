#include "builder.h"
#include "builtin.h"

#include <bit>
#include <deque>
#include <memory>

// parts to look at and to click while simulating. what they show is drawn
// by render/live.cpp from the values of their inputs, the geometry there
// matches the symbols here.

namespace logic::builtin {

namespace {

using model::direction;
using model::grid;
using sim::value;

const std::vector<std::string> colors{"red", "green", "yellow", "orange", "blue", "cyan", "white"};

// shows its inputs, does nothing in the simulation
class display_component : public sim::component {
public:
    void eval(std::span<value>, sim::context&) override {}
};

// measures how long each segment is lit, so multiplexed digits can be
// shown at their average brightness (see sample_view)
class seg7_component : public sim::component {
public:
    seg7_component(model::param_values params, std::vector<model::port> ports)
        : params(std::move(params))
        , ports(std::move(ports))
    {
    }

    void eval(std::span<value> values, sim::context& ctx) override
    {
        advance(ctx.now());
        int levels[8];
        seg7_levels(params, ports, values, levels);
        for (int k = 0; k < 8; k++)
            lit[k] = levels[k] == 1;
    }

    void reset() override
    {
        std::fill(std::begin(lit), std::end(lit), false);
        std::fill(std::begin(lit_time), std::end(lit_time), 0);
        last = window_start = 0;
    }

    bool has_view() const override { return true; }

    void sample_view(sim::time now, std::vector<float>& out) override
    {
        advance(now);
        out.resize(8);
        auto window = now - window_start;
        for (int k = 0; k < 8; k++)
            out[k] = window > 0 ? float(double(lit_time[k]) / double(window)) : (lit[k] ? 1.0f : 0.0f);
        std::fill(std::begin(lit_time), std::end(lit_time), 0);
        window_start = now;
    }

private:
    // the segments lit since `last` were lit until now
    void advance(sim::time now)
    {
        if (now <= last) return;
        for (int k = 0; k < 8; k++)
            if (lit[k]) lit_time[k] += now - last;
        last = now;
    }

    model::param_values params;
    std::vector<model::port> ports;
    bool lit[8] = {};
    sim::time lit_time[8] = {};
    sim::time last = 0;
    sim::time window_start = 0;
};

class led : public primitive {
public:
    std::string kind() const override { return "led"; }
    std::string name() const override { return "LED"; }
    std::string category() const override { return "Input/Output"; }
    std::string description() const override { return "lights up while its input is 1"; }

    std::vector<model::param_def> params() const override
    {
        return {
            model::choice_param("color", "Color", colors, "red"),
            model::bool_param("active_low", "Active low", false, "lights up while the input is 0"),
        };
    }

    model::cell_interface build(const model::param_values&) const override
    {
        builder b;
        b.circle({0, 0}, 7.0f);
        b.line({-2 * grid, 0}, {-7, 0});
        b.port("in", direction::input, 1, {-2 * grid, 0}, "1 lights it up");
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values&) const override { return std::make_unique<display_component>(); }
};

// segments a to g and the decimal point, from a bus, separate pins or a hex
// digit. render/live.cpp draws the segments inside the box.
class seg7_display : public primitive {
public:
    std::string kind() const override { return "seg7_display"; }
    std::string name() const override { return "7-segment display"; }
    std::string category() const override { return "Input/Output"; }
    std::string description() const override { return "shows segments a to g and a decimal point, from a bus, separate pins or a hex digit"; }

    std::vector<model::param_def> params() const override
    {
        return {
            model::choice_param("input", "Input", {"bus", "pins", "hex"}, "bus",
                "bus: segments a (bit 0) to g (bit 6), pins: one pin a segment, hex: a 4-bit digit shown as 0 to F"),
            model::choice_param("side", "Pin side", {"left", "right", "top", "bottom"}, "left", "where the bus pin is (bus and hex input)"),
            model::choice_param("color", "Color", colors, "red"),
            model::bool_param("dp", "Decimal point", false, "an 8th bit of the bus, or a dp pin"),
            model::bool_param("active_low", "Active low", false, "segments light up at 0, for common anode displays. only the point for hex input"),
            model::bool_param("enable", "Enable pin", false, "an en pin, dark unless it is 1 (for multiplexed displays)"),
            model::int_param("persistence", "Persistence (ms)", 100, 0, 2000,
                "how long a segment stays lit after it goes dark, so multiplexed digits stay visible"),
        };
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        auto input = model::get_string(v, "input");
        bool dp = model::get_bool(v, "dp");
        constexpr float label_size = 6.0f;
        builder b;
        b.rect({-2 * grid, -35}, {3 * grid, 35});

        if (input == "pins") {
            const char* names[] = {"a", "b", "c", "d", "e", "f", "g"};
            for (int k = 0; k < 7; k++) {
                int y = (3 - k) * grid;
                b.line({-3 * grid, y}, {-2 * grid, y});
                b.text({-18, y - 2}, names[k], label_size);
                b.port(names[k], direction::input, 1, {-3 * grid, y}, std::string("segment ") + names[k]);
            }
        } else {
            bool hex = input == "hex";
            int width = hex ? 4 : dp ? 8 : 7;
            auto side = model::get_string(v, "side");
            // the top and bottom edges are half a grid off, those stubs are
            // longer so the pin is on the grid
            glm::ivec2 pin{-3 * grid, 0}, edge{-2 * grid, 0};
            if (side == "right") pin = {4 * grid, 0}, edge = {3 * grid, 0};
            if (side == "top") pin = {grid, 5 * grid}, edge = {grid, 35};
            if (side == "bottom") pin = {grid, -5 * grid}, edge = {grid, -35};
            b.line(pin, edge, model::bus_stroke);
            if (hex) b.port("in", direction::input, 4, pin, "the digit to show, 0 to F");
            else b.port("seg", direction::input, width, pin, "segments a (bit 0) to g (bit 6)" + std::string(width == 8 ? ", point (bit 7)" : ""));
        }
        // the point's pin next to it
        if (dp && input != "bus") {
            b.line({3 * grid, -2 * grid}, {4 * grid, -2 * grid});
            b.port("dp", direction::input, 1, {4 * grid, -2 * grid}, "the decimal point");
        }
        if (model::get_bool(v, "enable")) {
            b.line({0, -5 * grid}, {0, -35});
            b.text({0, -32}, "en", label_size, model::text_align::center);
            b.port("en", direction::input, 1, {0, -5 * grid}, "1 shows the segments, 0 keeps them dark");
        }
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<seg7_component>(v, build(v).ports);
    }
};

// the value of a bus in hex
class hex_display : public primitive {
public:
    std::string kind() const override { return "hex_display"; }
    std::string name() const override { return "Hex display"; }
    std::string category() const override { return "Input/Output"; }
    std::string description() const override { return "shows its input in hex"; }

    std::vector<model::param_def> params() const override { return {model::int_param("width", "Bit width", 8, 1, 64)}; }

    model::cell_interface build(const model::param_values& v) const override
    {
        int width = (int)model::get_int(v, "width");
        int digits = (width + 3) / 4;
        int w = std::max(2 * grid, ((digits * 6 + 9) / grid + 1) * grid / 2 * 2);
        builder b;
        b.rect({0, -grid}, {w, grid});
        b.line({-grid, 0}, {0, 0}, width > 1 ? model::bus_stroke : 0.0f);
        b.port("in", direction::input, width, {-grid, 0}, "the value to show");
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values&) const override { return std::make_unique<display_component>(); }
};

// out is 1 while pressed, or toggles on each click
class button_component : public sim::component {
public:
    button_component(bool toggle, bool active_low)
        : toggle(toggle)
        , active_low(active_low)
    {
    }

    void eval(std::span<value> ports, sim::context&) override { ports[0] = sim::known(on != active_low, 1); }

    void reset() override { on = false; }

    bool interact(const std::string& action, int64_t v) override
    {
        bool before = on;
        if (action == "press") on = toggle ? !on : true;
        else if (action == "release" && !toggle) on = false;
        else if (action == "set") on = v != 0;
        return on != before;
    }

    void inspect(std::vector<sim::inspect_item>& out) const override { out.push_back({toggle ? "on" : "pressed", sim::known(on, 1), {}}); }

private:
    bool toggle;
    bool active_low;
    bool on = false;
};

class button : public primitive {
public:
    explicit button(bool toggle)
        : toggle(toggle)
    {
    }

    std::string kind() const override { return toggle ? "switch" : "button"; }
    std::string name() const override { return toggle ? "Switch" : "Button"; }
    std::string category() const override { return "Input/Output"; }
    std::string description() const override
    {
        return toggle ? "click to switch between 0 and 1 while simulating" : "1 while held down with the mouse while simulating";
    }

    std::vector<model::param_def> params() const override
    {
        return {model::bool_param("active_low", "Active low", false, toggle ? "0 when on" : "0 while pressed")};
    }

    model::cell_interface build(const model::param_values&) const override
    {
        builder b;
        b.rect({-grid, -grid}, {grid, grid});
        if (toggle) b.line({-5, 0}, {5, 0});
        else b.circle({0, 0}, 5.0f);
        b.line({grid, 0}, {2 * grid, 0});
        b.port("out", direction::output, 1, {2 * grid, 0}, toggle ? "1 when on" : "1 while pressed");
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<button_component>(toggle, model::get_bool(v, "active_low"));
    }

private:
    bool toggle;
};

// a serial terminal: what comes in on rx is shown, typed keys go out on tx.
// 8 data bits, no parity, 1 stop bit, idle at 1.
class terminal_component : public sim::component {
public:
    explicit terminal_component(sim::time bit_time)
        : bit_time(bit_time)
    {
        reset();
    }

    void reset() override
    {
        screen.clear();
        rx_bit = -1;
        prev_rx = 1;
        tx_queue.clear();
        tx_bit = -1;
        tx_level = 1;
    }

    void eval(std::span<value> ports, sim::context& ctx) override
    {
        auto now = ctx.now();
        int rx = ports[0].get(0) == sim::bit_state::zero ? 0 : 1;

        // receiving: sample each bit in its middle
        if (rx_bit >= 0 && ctx.woken() && rx_next <= now) {
            if (rx_bit < 8) {
                rx_byte |= (rx ? 1 : 0) << rx_bit;
                rx_bit++;
                rx_next += bit_time;
            } else {
                // the stop bit, a 0 there is a framing error and is dropped
                if (rx) received((char)rx_byte);
                rx_bit = -1;
            }
        } else if (rx_bit < 0 && prev_rx == 1 && rx == 0) {
            // a start bit: the first data bit is 1.5 bits later
            rx_bit = 0;
            rx_byte = 0;
            rx_next = now + bit_time + bit_time / 2;
        }
        prev_rx = rx;

        // sending
        if (tx_bit >= 0 && ctx.woken() && tx_next <= now) {
            tx_bit++;
            if (tx_bit == 10) {
                tx_bit = -1;
                tx_level = 1;
            } else {
                tx_level = (tx_frame >> tx_bit) & 1;
                tx_next += bit_time;
            }
        }
        if (tx_bit < 0 && !tx_queue.empty()) {
            // start, data from bit 0, stop
            tx_frame = (uint32_t(tx_queue.front()) << 1) | (1u << 9);
            tx_queue.pop_front();
            tx_bit = 0;
            tx_level = 0;
            tx_next = now + bit_time;
        }

        sim::time wake = sim::never;
        if (rx_bit >= 0) wake = std::min(wake, rx_next);
        if (tx_bit >= 0) wake = std::min(wake, tx_next);
        ctx.wake_at(wake);
        ports[1] = sim::known(tx_level, 1);
    }

    bool interact(const std::string& action, int64_t v) override
    {
        if (action == "key") {
            tx_queue.push_back(uint8_t(v));
            return true;
        }
        if (action == "clear") screen.clear();
        return false;
    }

    void inspect(std::vector<sim::inspect_item>& out) const override { out.push_back({"screen", {}, screen}); }

private:
    void received(char c)
    {
        if (c == '\r') return;
        if (c == '\b' || c == 127) {
            if (!screen.empty() && screen.back() != '\n') screen.pop_back();
            return;
        }
        screen += c;
        // only the end is kept
        if (screen.size() > max_screen) screen.erase(0, screen.size() - max_screen);
    }

    static constexpr size_t max_screen = 16000;

    sim::time bit_time;
    std::string screen;

    int rx_bit = -1;
    int rx_byte = 0;
    int prev_rx = 1;
    sim::time rx_next = 0;

    std::deque<uint8_t> tx_queue;
    uint32_t tx_frame = 0;
    int tx_bit = -1;
    int tx_level = 1;
    sim::time tx_next = 0;
};

class terminal : public primitive {
public:
    std::string kind() const override { return "terminal"; }
    std::string name() const override { return "Terminal"; }
    std::string category() const override { return "Input/Output"; }
    std::string description() const override { return "a serial terminal: shows what comes in on rx, sends typed keys on tx"; }

    std::vector<model::param_def> params() const override
    {
        return {model::int_param("baud", "Baud rate", 9600, 1, 100000000, "bits per second, 8 data bits, no parity, 1 stop bit")};
    }

    model::cell_interface build(const model::param_values&) const override
    {
        builder b;
        b.rect({-2 * grid, -2 * grid}, {4 * grid, 2 * grid});
        b.text({-12, -2}, "TERM", 7.0f);
        b.line({-3 * grid, grid}, {-2 * grid, grid});
        b.text({-17, 8}, "rx", 5.0f);
        b.port("rx", direction::input, 1, {-3 * grid, grid}, "received, shown on the screen");
        b.line({-3 * grid, -grid}, {-2 * grid, -grid});
        b.text({-17, -12}, "tx", 5.0f);
        b.port("tx", direction::output, 1, {-3 * grid, -grid}, "typed keys, 1 while idle");
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<terminal_component>(std::max<sim::time>(2, sim::seconds / (sim::time)model::get_int(v, "baud")));
    }
};

// rgba (red in the low byte) of a framebuffer's mono pixels
uint32_t mono_rgba(const std::string& color)
{
    static const std::pair<const char*, uint32_t> table[] = {
        {"red", 0xFF3030FF},
        {"green", 0x30FF30FF},
        {"yellow", 0xFFFF30FF},
        {"orange", 0xFFA020FF},
        {"blue", 0x4060FFFF},
        {"cyan", 0x30FFFFFF},
    };
    uint32_t rgb = 0xFFFFFFFF;
    for (auto& [name, c] : table)
        if (color == name) rgb = c;
    // 0xRRGGBBAA to bytes r, g, b, a in memory
    return (rgb >> 24) | ((rgb >> 8) & 0xFF00) | ((rgb << 8) & 0xFF0000) | 0xFF000000u;
}

int framebuffer_data_width(const std::string& format)
{
    return format == "mono" ? 1 : format == "grey8" ? 8 : 24;
}

// bits to address 0 .. n - 1, at least 1
int address_width(int n)
{
    return std::max(1, (int)std::bit_width(unsigned(n - 1)));
}

// clk, we, rst, x, y, data. on the rising edge of clk: rst 1 clears the
// picture to black, otherwise we 1 (or not connected) writes data at (x, y).
class framebuffer_component : public sim::component {
public:
    framebuffer_component(int width, int height, std::string format, uint32_t mono)
        : buf(std::make_shared<sim::pixel_buffer>(width, height))
        , format(std::move(format))
        , mono(mono)
    {
    }

    void eval(std::span<value> ports, sim::context&) override
    {
        auto clk = ports[0].get(0);
        bool rising = prev_clk == sim::bit_state::zero && clk == sim::bit_state::one;
        prev_clk = clk;
        if (!rising) return;

        auto we = ports[1].get(0), rst = ports[2].get(0);
        if (rst == sim::bit_state::one) {
            clear();
            return;
        }
        // an unknown control or address writes nothing
        if (rst == sim::bit_state::x || we == sim::bit_state::zero || we == sim::bit_state::x) return;
        if (!ports[3].fully_known() || !ports[4].fully_known()) return;
        auto x = ports[3].to_u64(), y = ports[4].to_u64();
        if (x >= (uint64_t)buf->width || y >= (uint64_t)buf->height) return;

        uint32_t c = color_of(ports[5]);
        std::lock_guard lk(buf->m);
        buf->rgba[y * buf->width + x] = c;
        buf->version++;
        written++;
    }

    void reset() override
    {
        clear();
        prev_clk = sim::bit_state::z;
        written = 0;
    }

    void inspect(std::vector<sim::inspect_item>& out) const override
    {
        out.push_back({"pixels written", sim::known(written, 64), {}});
    }

    std::shared_ptr<sim::pixel_buffer> pixels() const override { return buf; }

    // shown for data with unknown bits
    static constexpr uint32_t marker = 0xFFFF00FFu;

private:
    uint32_t color_of(const value& d) const
    {
        if (!d.fully_known()) return marker;
        auto v = d.to_u64();
        if (format == "mono") return v & 1 ? mono : 0xFF000000u;
        if (format == "grey8") return uint32_t(v & 0xFF) * 0x010101u | 0xFF000000u;
        // 0xRRGGBB to bytes r, g, b
        return uint32_t((v >> 16) & 0xFF) | uint32_t(v & 0xFF00) | uint32_t((v & 0xFF) << 16) | 0xFF000000u;
    }

    void clear()
    {
        std::lock_guard lk(buf->m);
        std::fill(buf->rgba.begin(), buf->rgba.end(), 0xFF000000u);
        buf->version++;
    }

    std::shared_ptr<sim::pixel_buffer> buf;
    std::string format;
    uint32_t mono;
    sim::bit_state prev_clk = sim::bit_state::z;
    uint64_t written = 0;
};

class framebuffer : public primitive {
public:
    std::string kind() const override { return "framebuffer"; }
    std::string name() const override { return "Framebuffer"; }
    std::string category() const override { return "Input/Output"; }
    std::string description() const override { return "a picture of width x height pixels, written one pixel per clock and shown in a window while simulating"; }

    std::vector<model::param_def> params() const override
    {
        return {
            model::int_param("width", "Width", 64, 1, 1024, "pixels across"),
            model::int_param("height", "Height", 64, 1, 1024, "pixels down"),
            model::choice_param("format", "Format", {"rgb888", "grey8", "mono"}, "rgb888",
                "rgb888: 24 bits, red in bits 23-16, green 15-8, blue 7-0. grey8: 8 bits from black to white. mono: 1 bit, lit or black"),
            model::choice_param("color", "Mono color", colors, "white", "the color of lit pixels in mono"),
        };
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int w = (int)model::get_int(v, "width"), h = (int)model::get_int(v, "height");
        int dw = framebuffer_data_width(model::get_string(v, "format"));
        builder b;
        b.box("FB",
            {
                {"clk", direction::input, 1, "writes or clears on the rising edge", ">"},
                {"we", direction::input, 1, "1 or not connected: data is written at x, y on the clock edge"},
                {"rst", direction::input, 1, "1 clears the picture to black on the clock edge"},
                {"x", direction::input, address_width(w), "column, 0 at the left. outside the picture nothing is written"},
                {"y", direction::input, address_width(h), "row, 0 at the top. outside the picture nothing is written"},
                {"data", direction::input, dw, "the pixel's color, see the format"},
            },
            {});
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<framebuffer_component>((int)model::get_int(v, "width"), (int)model::get_int(v, "height"),
            model::get_string(v, "format"), mono_rgba(model::get_string(v, "color")));
    }
};

}

uint8_t seg7_hex(int digit)
{
    static const uint8_t patterns[16] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71};
    return patterns[digit & 15];
}

void seg7_levels(const model::param_values& params, const std::vector<model::port>& ports, std::span<const sim::value> values, int out[8])
{
    auto text = [&](const char* name) {
        auto it = params.find(name);
        return it != params.end() && std::get_if<std::string>(&it->second) ? std::get<std::string>(it->second) : std::string();
    };
    auto flag = [&](const char* name) {
        auto it = params.find(name);
        return it != params.end() && std::get_if<bool>(&it->second) && std::get<bool>(it->second);
    };
    // 1, 0 or -1 for x, floating counts as 0
    auto level = [](const sim::value& v, int bit, bool active_low) {
        auto b = v.get(bit);
        if (b == sim::bit_state::z) return 0;
        if (b == sim::bit_state::x) return -1;
        return (b == sim::bit_state::one) != active_low ? 1 : 0;
    };
    // the value of a port, none while not simulating
    auto value_of = [&](const std::string& name) -> const sim::value* {
        for (size_t k = 0; k < ports.size() && k < values.size(); k++)
            if (ports[k].name == name) return &values[k];
        return nullptr;
    };

    std::fill(out, out + 8, 0);
    bool low = flag("active_low");
    auto input = text("input");
    if (input == "hex") {
        // partly unknown: every segment unknown
        if (auto v = value_of("in"); v && !v->fully_floating()) {
            uint8_t on = seg7_hex((int)v->to_u64());
            for (int k = 0; k < 7; k++)
                out[k] = !v->fully_known() ? -1 : (on >> k) & 1;
        }
    } else if (input == "pins") {
        const char* names[] = {"a", "b", "c", "d", "e", "f", "g"};
        for (int k = 0; k < 7; k++)
            if (auto v = value_of(names[k])) out[k] = level(*v, 0, low);
    } else if (auto v = value_of("seg")) {
        for (int k = 0; k < std::min(v->width(), 8); k++)
            out[k] = level(*v, k, low);
    }
    if (input != "bus")
        if (auto v = value_of("dp")) out[7] = level(*v, 0, low);
    if (flag("enable")) {
        auto v = value_of("en");
        int en = v ? level(*v, 0, false) : 0;
        for (int k = 0; k < 8; k++)
            out[k] = en == 1 ? out[k] : en == 0 ? 0 : -1;
    }
}

void add_io(model::primitive_registry& r)
{
    r.add(std::make_unique<led>());
    r.add(std::make_unique<seg7_display>());
    r.add(std::make_unique<hex_display>());
    r.add(std::make_unique<button>(false));
    r.add(std::make_unique<button>(true));
    r.add(std::make_unique<terminal>());
    r.add(std::make_unique<framebuffer>());
}

}
