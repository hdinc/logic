// a tiny 8 bit processor to show the debugging side of the sdk: firmware
// from a file parameter, memory, program counter, disassembly, breakpoints
// and single steps.
//
// 256 bytes of memory, an accumulator a and a program counter. one
// instruction per rising edge of clk:
//
//   00       nop
//   01 n     ldi n      a = n
//   02 n     addi n     a = a + n
//   03       out        port = a
//   04 x     jmp x
//   05 x     jnz x      jump if a isn't 0
//   06       dec        a = a - 1
//   ff       halt

#include "logic_model.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <set>

namespace {

class toy_cpu : public logic_sdk::instance {
public:
    toy_cpu(const logic_params&, const logic_host&) { reset(); }

    void reset() override
    {
        mem = rom;
        a = 0;
        pc_ = 0;
        port = 0;
        halted = false;
        prev_clk = 3;
        skip_break = false;
        step = false;
    }

    void eval(logic_value* ports, logic_context& ctx) override
    {
        int clk = logic_get_bit(&ports[0], 0);
        if (prev_clk == 0 && clk == 1 && !halted) {
            if (breakpoints.count(pc_) && !skip_break) {
                // stops before the instruction, it runs when going on
                skip_break = true;
                ctx.pause = "breakpoint";
            } else {
                skip_break = false;
                execute();
                if (step) {
                    step = false;
                    ctx.pause = "step";
                }
            }
        }
        prev_clk = clk;
        logic_set_u64(&ports[1], port);
        logic_set_u64(&ports[2], halted);
    }

    void inspect(const logic_inspect_sink& s) override
    {
        logic_sdk::small_value v;
        v.bits = a;
        auto av = v.view(8);
        s.value(s.data, "a", &av);
        logic_sdk::small_value p;
        p.bits = pc_;
        auto pv = p.view(8);
        s.value(s.data, "pc", &pv);
    }

    bool load_image(const char* param, const logic_image& image, const logic_host&) override
    {
        if (std::strcmp(param, "firmware") != 0) return false;
        rom.fill(0xFF);
        for (uint32_t k = 0; k < image.segment_count; k++) {
            auto& s = image.segments[k];
            for (uint64_t i = 0; i < s.size; i++)
                if (s.address + i < rom.size()) rom[s.address + i] = s.bytes[i];
        }
        reset();
        return true;
    }

    std::vector<logic_memory_info> memories() override { return {{"memory", mem.size()}}; }

    bool read_memory(uint32_t k, uint64_t address, uint8_t* out, uint64_t n) override
    {
        if (k != 0 || address + n > mem.size()) return false;
        std::memcpy(out, mem.data() + address, n);
        return true;
    }

    bool write_memory(uint32_t k, uint64_t address, const uint8_t* data, uint64_t n) override
    {
        if (k != 0 || address + n > mem.size()) return false;
        std::memcpy(mem.data() + address, data, n);
        return true;
    }

    bool pc(uint64_t& out) override
    {
        out = pc_;
        return true;
    }

    uint32_t disassemble(uint64_t address, std::string& text) override
    {
        if (address >= mem.size()) return 0;
        uint8_t op = mem[address], arg = mem[(address + 1) % mem.size()];
        char buf[32];
        auto with_arg = [&](const char* name) {
            std::snprintf(buf, sizeof(buf), "%s 0x%02X", name, arg);
            text = buf;
            return 2u;
        };
        switch (op) {
        case 0x00: text = "nop"; return 1;
        case 0x01: return with_arg("ldi");
        case 0x02: return with_arg("addi");
        case 0x03: text = "out"; return 1;
        case 0x04: return with_arg("jmp");
        case 0x05: return with_arg("jnz");
        case 0x06: text = "dec"; return 1;
        case 0xFF: text = "halt"; return 1;
        default: std::snprintf(buf, sizeof(buf), ".byte 0x%02X", op); text = buf; return 1;
        }
    }

    void set_breakpoint(uint64_t address, bool on) override
    {
        if (on) breakpoints.insert((uint8_t)address);
        else breakpoints.erase((uint8_t)address);
    }

    void step_instruction() override { step = true; }

private:
    void execute()
    {
        auto next = [&] { return mem[pc_++]; };
        switch (next()) {
        case 0x01: a = next(); break;
        case 0x02: a = uint8_t(a + next()); break;
        case 0x03: port = a; break;
        case 0x04: pc_ = next(); break;
        case 0x05: {
            uint8_t x = next();
            if (a) pc_ = x;
            break;
        }
        case 0x06: a--; break;
        case 0xFF:
            pc_--;
            halted = true;
            break;
        default: break;
        }
    }

    std::array<uint8_t, 256> rom{}, mem{};
    uint8_t a = 0, pc_ = 0, port = 0;
    bool halted = false;
    int prev_clk = 3;
    std::set<uint8_t> breakpoints;
    bool skip_break = false;
    bool step = false;
};

const logic_param_desc params[] = {
    logic_sdk::file_param("firmware", "Firmware", "program file: intel hex, s-records, elf or binary"),
};

int ports(const logic_params*, const logic_port_sink* s)
{
    logic_sdk::add_port(s, "clk", LOGIC_INPUT, 1, "one instruction per rising edge");
    logic_sdk::add_port(s, "port", LOGIC_OUTPUT, 8, "written by out");
    logic_sdk::add_port(s, "halted", LOGIC_OUTPUT, 1, "1 after halt");
    return 1;
}

}

// a function: a global would be initialized in no particular order with the
// plugin's list in examples.cpp
const logic_model_desc& example_toy_cpu()
{
    static const auto d = logic_sdk::make_model<toy_cpu>("toy_cpu", "a tiny 8 bit processor", "Examples", params, 1, ports);
    return d;
}
