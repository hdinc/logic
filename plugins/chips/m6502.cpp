// mos 6502 on its pins, with the cycle-stepped core of floooh/chips.
//
// one bus cycle per clock period. clk is phi2: a cycle ends on the falling
// edge. then the cpu takes d if it was a read, sets we to 0, runs one cycle
// and puts the next address, rw and sync out a little later (hold, 1 ns),
// so memories see we go to 0 before the address changes. a write cycle
// drives d and sets we from then on until it ends. that fits the ram
// builtin, which lets go of the bus when we is 1.
//
// res_n, irq_n and nmi_n are active low, rdy 0 stops the cpu at the next
// read. not connected inputs are inactive.
//
// the debugger sees the cpu's bus: memory is what was read or written so
// far, not the memories on the schematic.

#include "logic_model.hpp"

#include <array>
#include <cstring>
#include <set>

#define CHIPS_IMPL
#define CHIPS_UTIL_IMPL
#include "third_party/chips/m6502.h"
#include "third_party/chips/m6502dasm.h"

namespace {

enum port_index { clk, res_n, irq_n, nmi_n, rdy, a, d, rw, we, sync, port_count };

const uint64_t hold = LOGIC_NS;

class m6502 : public logic_sdk::instance {
public:
    m6502(const logic_params& p, const logic_host&)
    {
        desc.bcd_disabled = std::strcmp(logic_param_text(&p, "variant", "6502"), "6502") != 0;
        reset();
    }

    void reset() override
    {
        s = {};
        s.pins = m6502_init(&s.cpu, &desc);
        s.prev_clk = 3;
        step = false;
    }

    void eval(logic_value* ports, logic_context& ctx) override
    {
        if (ctx.woken && s.pending) {
            start_cycle(ctx);
            ctx.wake = LOGIC_NEVER;
        }

        int c = logic_get_bit(&ports[clk], 0);
        if (s.prev_clk == 1 && c == 0) end_cycle(ports, ctx);
        s.prev_clk = c;

        logic_set_u64(&ports[a], s.address);
        if (s.drive) logic_set_u64(&ports[d], s.data);
        else logic_set_z(&ports[d]);
        logic_set_u64(&ports[rw], s.read);
        logic_set_u64(&ports[we], s.we);
        logic_set_u64(&ports[sync], s.sync);
    }

    void inspect(const logic_inspect_sink& sink) override
    {
        auto put = [&](const char* name, uint64_t v, uint32_t width) {
            logic_sdk::small_value sv;
            sv.bits = v;
            auto view = sv.view(width);
            sink.value(sink.data, name, &view);
        };
        put("a", s.cpu.A, 8);
        put("x", s.cpu.X, 8);
        put("y", s.cpu.Y, 8);
        put("s", s.cpu.S, 8);
        put("p", s.cpu.P, 8);
        put("pc", s.instruction, 16);
        put("cycles", s.cycles, 64);
    }

    // the cpu and the bus state, not the bytes seen by the debugger
    bool save_state(const logic_state_writer& w) override
    {
        auto copy = s;
        m6502_snapshot_onsave(&copy.cpu);
        logic_sdk::write(w, copy);
        return true;
    }

    bool load_state(const void* bytes, size_t size) override
    {
        if (size != sizeof(state)) return false;
        state loaded;
        std::memcpy(&loaded, bytes, size);
        m6502_snapshot_onload(&loaded.cpu, &s.cpu);
        s = loaded;
        return true;
    }

    std::vector<logic_memory_info> memories() override { return {{"bus (seen so far)", bus.size()}}; }

    bool read_memory(uint32_t k, uint64_t address, uint8_t* out, uint64_t n) override
    {
        if (k != 0 || address + n > bus.size()) return false;
        std::memcpy(out, bus.data() + address, n);
        return true;
    }

    bool pc(uint64_t& out) override
    {
        out = s.instruction;
        return true;
    }

    uint32_t disassemble(uint64_t address, std::string& text) override
    {
        if (address >= bus.size()) return 0;
        struct io {
            const std::array<uint8_t, 0x10000>* bus;
            uint16_t at;
            std::string* text;
        } d{&bus, (uint16_t)address, &text};
        text.clear();
        auto in = [](void* p) {
            auto io_ = static_cast<io*>(p);
            return (*io_->bus)[io_->at++];
        };
        auto out = [](char ch, void* p) { static_cast<io*>(p)->text->push_back(ch); };
        auto next = m6502dasm_op((uint16_t)address, in, out, &d);
        return uint16_t(next - address);
    }

    void set_breakpoint(uint64_t address, bool on) override
    {
        if (on) breakpoints.insert((uint16_t)address);
        else breakpoints.erase((uint16_t)address);
    }

    void step_instruction() override { step = true; }

private:
    // the falling edge of clk
    void end_cycle(logic_value* ports, logic_context& ctx)
    {
        // a clock faster than the hold time
        if (s.pending) start_cycle(ctx);

        if (s.started && s.read) {
            // x and z bits read as 1
            auto& v = ports[d];
            uint8_t byte = uint8_t(v.bits[0] | v.x[0] | v.z[0]);
            M6502_SET_DATA(s.pins, byte);
            bus[s.address] = byte;
        }
        s.we = false;

        if (logic_get_bit(&ports[res_n], 0) == 0) {
            // held in reset, starts with the reset sequence when let go
            s.pins = m6502_init(&s.cpu, &desc);
            s.started = false;
            s.next = {0, true, false};
        } else {
            uint64_t in = 0;
            if (logic_get_bit(&ports[irq_n], 0) == 0) in |= M6502_IRQ;
            if (logic_get_bit(&ports[nmi_n], 0) == 0) in |= M6502_NMI;
            if (logic_get_bit(&ports[rdy], 0) == 0) in |= M6502_RDY;
            s.stalled = (in & M6502_RDY) && (s.pins & M6502_RW);
            s.pins = m6502_tick(&s.cpu, (s.pins & ~(M6502_IRQ | M6502_NMI | M6502_RDY)) | in);
            s.cycles++;
            s.started = true;
            s.next = {M6502_GET_ADDR(s.pins), (s.pins & M6502_RW) != 0, (s.pins & M6502_SYNC) != 0};
        }
        s.pending = true;
        ctx.wake = ctx.now + hold;
    }

    // after the hold time: the next cycle's address and direction
    void start_cycle(logic_context& ctx)
    {
        s.pending = false;
        s.address = s.next.address;
        s.read = s.next.read;
        s.sync = s.next.sync;
        s.data = M6502_GET_DATA(s.pins);
        s.drive = s.we = s.started && !s.read;
        if (s.we) bus[s.address] = s.data;

        if (s.sync && s.started && !s.stalled) {
            s.instruction = s.address;
            if (step) {
                step = false;
                ctx.pause = "step";
            } else if (breakpoints.count(s.address)) {
                ctx.pause = "breakpoint";
            }
        }
    }

    struct cycle {
        uint16_t address;
        bool read, sync;
    };

    // plain data for save_state
    struct state {
        m6502_t cpu;
        uint64_t pins;
        uint64_t cycles;
        // the pins as seen outside
        uint16_t address;
        uint8_t data;
        bool read, sync, we, drive;
        // a cycle ran since reset
        bool started;
        // rdy repeated the last read
        bool stalled;
        // next is put out at the wake time
        bool pending;
        cycle next;
        uint16_t instruction;
        int prev_clk;
    };

    m6502_desc_t desc{};
    state s{};
    std::array<uint8_t, 0x10000> bus{};
    std::set<uint16_t> breakpoints;
    bool step = false;
};

const char* const variants[] = {"6502", "2A03"};

const logic_param_desc params[] = {
    logic_sdk::choice_param("variant", "Variant", variants, "6502", "2A03: the nes cpu, without decimal mode"),
};

int ports(const logic_params*, const logic_port_sink* sink)
{
    using logic_sdk::add_port;
    add_port(sink, "clk", LOGIC_INPUT, 1, "phi2: one bus cycle per period, a cycle ends on the falling edge");
    add_port(sink, "res_n", LOGIC_INPUT, 1, "0 holds the cpu in reset, it starts at the reset vector (fffc) when let go");
    add_port(sink, "irq_n", LOGIC_INPUT, 1, "0 requests an interrupt (vector fffe) unless the i flag is set");
    add_port(sink, "nmi_n", LOGIC_INPUT, 1, "a falling edge requests a non-maskable interrupt (vector fffa)");
    add_port(sink, "rdy", LOGIC_INPUT, 1, "0 stops the cpu at the next read cycle");
    add_port(sink, "a", LOGIC_OUTPUT, 16, "address bus");
    add_port(sink, "d", LOGIC_INOUT, 8, "data bus, driven by the cpu in a write cycle");
    add_port(sink, "rw", LOGIC_OUTPUT, 1, "1 read, 0 write");
    add_port(sink, "we", LOGIC_OUTPUT, 1, "1 in a write cycle while the address and data are valid, for a ram's we");
    add_port(sink, "sync", LOGIC_OUTPUT, 1, "1 while fetching an opcode");
    return 1;
}

}

const logic_model_desc& chips_m6502()
{
    static const auto desc = logic_sdk::make_model<m6502>("mos6502", "MOS 6502 processor on its pins", "Processors", params, 1, ports);
    return desc;
}
