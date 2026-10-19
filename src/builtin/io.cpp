#include "builder.h"
#include "builtin.h"

#include <deque>

// parts to look at and to click while simulating. what they show is drawn
// by render/live.cpp from the values of their inputs, the geometry there
// matches the symbols here.

namespace logic::builtin {

namespace {

using model::direction;
using model::grid;
using sim::value;

const std::vector<std::string> colors{"red", "green", "yellow", "blue", "white"};

// shows its inputs, does nothing in the simulation
class display_component : public sim::component {
public:
    void eval(std::span<value>, sim::context&) override {}
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

// segments a to g and the decimal point
class seg7_display : public primitive {
public:
    std::string kind() const override { return "seg7_display"; }
    std::string name() const override { return "7-segment display"; }
    std::string category() const override { return "Input/Output"; }
    std::string description() const override { return "segments a (bit 0) to g (bit 6) and the decimal point (bit 7)"; }

    std::vector<model::param_def> params() const override
    {
        return {
            model::choice_param("color", "Color", colors, "red"),
            model::bool_param("dp", "Decimal point", false, "an 8th bit for the point"),
            model::bool_param("active_low", "Active low", false, "segments light up at 0, for common anode displays"),
        };
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int width = model::get_bool(v, "dp") ? 8 : 7;
        builder b;
        b.rect({-2 * grid, -3 * grid}, {2 * grid, 3 * grid});
        b.line({-3 * grid, 0}, {-2 * grid, 0}, model::bus_stroke);
        b.port("seg", direction::input, width, {-3 * grid, 0}, "segments a (bit 0) to g (bit 6)" + std::string(width == 8 ? ", point (bit 7)" : ""));
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values&) const override { return std::make_unique<display_component>(); }
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

}

void add_io(model::primitive_registry& r)
{
    r.add(std::make_unique<led>());
    r.add(std::make_unique<seg7_display>());
    r.add(std::make_unique<hex_display>());
    r.add(std::make_unique<button>(false));
    r.add(std::make_unique<button>(true));
    r.add(std::make_unique<terminal>());
}

}
