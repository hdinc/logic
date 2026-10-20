// an avr microcontroller on its pins, simulated by simavr

#pragma once

#include "logic_model.hpp"
#include "serial.h"

#include <string>
#include <vector>

struct avr_t;
struct avr_irq_t;

namespace avr {

struct pin_def {
    char port;
    int bit;
    const char* name;
    const char* description;
};

struct part {
    // model name in the plugin and simavr's name for the chip
    const char* model;
    const char* core;
    const char* description;
    uint32_t default_frequency;
    std::vector<pin_def> pins;
    // the uart on txd/rxd (indexes into pins), 0 if there is none
    char uart;
    int txd, rxd;
};

// ports: the pins in the order of part::pins, then reset_n
class mcu : public logic_sdk::instance {
public:
    mcu(const part& p, const logic_params& params, const logic_host& host);
    ~mcu() override;

    void reset() override;
    void eval(logic_value* ports, logic_context& ctx) override;
    void inspect(const logic_inspect_sink& sink) override;
    bool load_image(const char* param, const logic_image& image, const logic_host& host) override;

    std::vector<logic_memory_info> memories() override;
    bool read_memory(uint32_t k, uint64_t address, uint8_t* out, uint64_t n) override;
    bool write_memory(uint32_t k, uint64_t address, const uint8_t* data, uint64_t n) override;
    bool pc(uint64_t& out) override;
    uint32_t disassemble(uint64_t address, std::string& text) override;
    void set_breakpoint(uint64_t address, bool on) override;
    void step_instruction() override;

    // simavr's log and uart output
    void log(int level, const char* text);
    void uart_output(uint8_t byte);

private:
    struct port_state;

    // time of an avr cycle and back
    uint64_t time_of(uint64_t cycle) const;
    uint64_t cycle_at(uint64_t t) const;

    // one instruction and the cycle timers, without simavr's sleeping
    void step_one();
    // runs until an output changes, a pause, sleep without timers, or the
    // avr is past until
    void run(uint64_t until);
    // drives the avr's inputs from the pins (pull-ups inside the model)
    void apply_inputs();
    // looks at the port registers after an instruction: pending if the
    // pins have to change
    void take_registers();
    void write_ports(logic_value* ports, uint64_t now);
    uint64_t bit_time() const;
    bool tx_enabled() const;
    bool rx_enabled() const;
    void restart(uint64_t now);
    // frees the avr
    void release();
    void setup(int64_t frequency);

    const part& p;
    logic_host host;
    avr_t* a = nullptr;
    uint64_t period;
    uint64_t run_ahead;
    // flash contents from the firmware, kept over reset
    std::vector<uint8_t> flash;
    std::vector<uint8_t> eeprom;

    std::vector<port_state> ports_;
    // per pin: 0, 1, 2 x, 3 z from the net
    std::vector<int> input;
    // per pin as put out: drive and level
    std::vector<bool> drive, level;
    // what they change to when pending
    std::vector<bool> next_drive, next_level;

    // time of avr cycle 0, set by the first eval after a reset
    uint64_t base = 0;
    bool started = false;
    bool held = false;
    // outputs that changed at the avr's time, put out when it comes
    bool pending = false;
    // a pause to report at the avr's time
    const char* pause_reason = nullptr;
    // the time a pause was reported at
    uint64_t paused_at = LOGIC_NEVER;

    std::vector<uint8_t> breakpoints;
    int breakpoint_count = 0;
    // going on from a breakpoint runs the instruction there
    bool skip_breakpoint = false;
    bool step = false;

    avr_irq_t* uart_out = nullptr;
    avr_irq_t* uart_in = nullptr;
    void* uart = nullptr;
    serial_out tx;
    serial_in rx;
    std::string log_text;
};

}
