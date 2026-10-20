// example models for the sdk: a 7 segment decoder and a uart transmitter in
// c++, and the counter from counter.c. also used by the tests.

#include "logic_model.hpp"

#include <stdexcept>

extern "C" const logic_model_desc example_counter;
const logic_model_desc& example_toy_cpu();

namespace {

// 4 bit hex digit -> segments a (bit 0) to g (bit 6)
class seg7 : public logic_sdk::instance {
public:
    seg7(const logic_params& p, const logic_host&)
        : active_low(logic_param_int(&p, "active_low", 0) != 0)
    {
    }

    void eval(logic_value* ports, logic_context&) override
    {
        static const uint8_t digits[16] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71};
        if (!logic_is_known(&ports[0])) {
            logic_set_x(&ports[1]);
            return;
        }
        uint8_t s = digits[logic_get_u64(&ports[0]) & 15];
        logic_set_u64(&ports[1], active_low ? ~s & 0x7F : s);
    }

private:
    bool active_low;
};

const logic_param_desc seg7_params[] = {
    logic_sdk::bool_param("active_low", "Active low", false, "segments are on at 0, for common anode displays"),
};

int seg7_ports(const logic_params*, const logic_port_sink* s)
{
    logic_sdk::add_port(s, "digit", LOGIC_INPUT, 4, "hex digit to show");
    logic_sdk::add_port(s, "seg", LOGIC_OUTPUT, 7, "segments a (bit 0) to g (bit 6)");
    return 1;
}

// a symbol instead of the generated box: pins name their port, y is up,
// one grid is 10. a symbol edited in the program can be pasted here too,
// from <workspace>/plugins/logic_examples/symbols/seg7_decoder.json.
const char* const seg7_symbol = R"({
    "shapes": [
        {"type": "line", "a": [-20, 20], "b": [20, 20]},
        {"type": "line", "a": [20, 20], "b": [20, -20]},
        {"type": "line", "a": [20, -20], "b": [-20, -20]},
        {"type": "line", "a": [-20, -20], "b": [-20, 20]},
        {"type": "line", "a": [-30, 0], "b": [-20, 0], "width": 2.5},
        {"type": "line", "a": [20, 0], "b": [30, 0], "width": 2.5},
        {"type": "line", "a": [4, 12], "b": [12, 12]},
        {"type": "line", "a": [12, 12], "b": [12, -12]},
        {"type": "line", "a": [12, -12], "b": [4, -12]},
        {"type": "line", "a": [4, 0], "b": [12, 0]},
        {"type": "line", "a": [4, 12], "b": [4, -12]},
        {"type": "text", "pos": [-17, -2], "text": "hex", "size": 6},
        {"type": "text", "pos": [-20, 23], "text": "seg7_decoder", "size": 8}
    ],
    "pins": [
        {"port": "digit", "pos": [-30, 0]},
        {"port": "seg", "pos": [30, 0]}
    ]
})";

logic_model_desc seg7_model()
{
    auto d = logic_sdk::make_model<seg7>("seg7_decoder", "hex digit to 7 segments", "Examples", seg7_params, 1, seg7_ports);
    d.symbol = seg7_symbol;
    return d;
}

// data, send -> tx, busy. a rising edge on send while not busy sends data:
// a 0 start bit, 8 data bits from bit 0, a 1 stop bit, each 1/baud long.
// tx is 1 while idle.
class uart_tx : public logic_sdk::instance {
public:
    uart_tx(const logic_params& p, const logic_host&)
    {
        auto baud = logic_param_int(&p, "baud", 9600);
        if (baud <= 0) throw std::runtime_error("baud rate must be positive");
        bit_time = LOGIC_S / (uint64_t)baud;
        reset();
    }

    void reset() override
    {
        index = -1;
        prev_send = 3;
    }

    void eval(logic_value* ports, logic_context& ctx) override
    {
        int send = logic_get_bit(&ports[1], 0);

        if (ctx.woken && index >= 0) {
            if (++index == 10) {
                index = -1;
                ctx.wake = LOGIC_NEVER;
            } else {
                ctx.wake = ctx.now + bit_time;
            }
        }

        if (index < 0 && prev_send == 0 && send == 1) {
            // start, data, stop from bit 0
            frame = ((logic_get_u64(&ports[0]) & 0xFF) << 1) | (1u << 9);
            index = 0;
            ctx.wake = ctx.now + bit_time;
        }
        prev_send = send;

        logic_set_u64(&ports[2], index < 0 ? 1 : (frame >> index) & 1);
        logic_set_u64(&ports[3], index >= 0);
    }

    void inspect(const logic_inspect_sink& s) override
    {
        logic_sdk::small_value v;
        v.bits = (uint64_t)(index + 1);
        auto view = v.view(4);
        s.value(s.data, "bit", &view);
    }

private:
    uint64_t bit_time;
    uint32_t frame = 0;
    int index = -1;
    int prev_send = 3;
};

const logic_param_desc uart_params[] = {
    logic_sdk::int_param("baud", "Baud rate", 9600, 1, 100000000, "bits per second"),
};

int uart_ports(const logic_params*, const logic_port_sink* s)
{
    logic_sdk::add_port(s, "data", LOGIC_INPUT, 8, "byte to send");
    logic_sdk::add_port(s, "send", LOGIC_INPUT, 1, "a rising edge starts sending data");
    logic_sdk::add_port(s, "tx", LOGIC_OUTPUT, 1, "serial output, 1 while idle");
    logic_sdk::add_port(s, "busy", LOGIC_OUTPUT, 1, "1 while sending");
    return 1;
}

const logic_plugin* plugin()
{
    static const logic_model_desc models[] = {
        example_counter,
        seg7_model(),
        logic_sdk::make_model<uart_tx>("uart_tx", "sends bytes over a serial line", "Examples", uart_params, 1, uart_ports),
        example_toy_cpu(),
    };
    static const logic_plugin p = {LOGIC_MODEL_ABI_VERSION, "logic_examples", models, sizeof(models) / sizeof(models[0])};
    return &p;
}

}

extern "C" LOGIC_EXPORT const logic_plugin* logic_plugin_entry(uint32_t host_abi_version)
{
    return host_abi_version == LOGIC_MODEL_ABI_VERSION ? plugin() : nullptr;
}
