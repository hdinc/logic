#pragma once

// an rv32ima_zicsr_zifencei hart with machine, supervisor and user mode and
// sv32 paging. the architecture follows riscv-simulator by hdinc and
// uwrk9amtdn (github.com/uwrk9amtdn/riscv-simulator, smode branch); traps
// are return values here, memory is a bus that may ask to retry, and the
// counters, trap csrs, tvm/tw/tsr and misaligned accesses are added.

#include "decode.h"

#include <cstdint>
#include <optional>

namespace logic::riscv {

enum class access : uint8_t {
    read = 1,
    write = 2,
    execute = 4
};

enum class result : uint8_t {
    ok,
    // access fault
    fault,
    // not done yet, e.g. a cache miss: the hart retries the instruction
    // later without having changed anything
    stall,
    // the access isn't aligned and this memory can't do it unaligned
    misaligned
};

// the physical memory of a hart, e.g. its caches or a flat memory.
// accesses are 1 to 4 bytes, little endian, at any alignment. a load that
// stalled is asked again with the same arguments when the hart retries.
class bus {
public:
    virtual ~bus() = default;
    // kind is execute for fetches, write for the load of an amo
    virtual result load(uint32_t addr, int size, uint32_t& value, access kind) = 0;
    virtual result store(uint32_t addr, int size, uint32_t value) = 0;
    // fence.i: later fetches see earlier stores
    virtual void fence_i() {}
};

// mip bits
namespace ip {
constexpr uint32_t ssip = 1u << 1;
constexpr uint32_t msip = 1u << 3;
constexpr uint32_t stip = 1u << 5;
constexpr uint32_t mtip = 1u << 7;
constexpr uint32_t seip = 1u << 9;
constexpr uint32_t meip = 1u << 11;
}

enum class step_result {
    retired,
    // an exception or interrupt was taken, pc is at the handler
    trapped,
    // memory asked to retry, nothing changed
    stalled,
    // in wfi without a pending interrupt, nothing changed
    waiting
};

class hart {
public:
    explicit hart(bus& memory, uint32_t hartid = 0);

    // machine mode at pc, csrs at their reset values, registers zero
    void reset(uint32_t pc);
    step_result step();

    // the interrupt lines: msip, mtip, meip and seip (the plic's supervisor
    // context, or'ed into mip.seip as the spec says)
    void set_lines(uint32_t lines) { m_lines = lines & (ip::msip | ip::mtip | ip::meip | ip::seip); }
    uint32_t lines() const { return m_lines; }
    // interrupts that are pending and enabled in mie, whether or not they
    // can be taken now: they end a wfi
    bool interrupt_pending() const { return (mip() & mie) != 0; }

    // csr read for debugging views, no side effects or privilege checks
    std::optional<uint32_t> read_csr(uint32_t csr) const;
    uint32_t mip() const { return m_mip | m_lines; }

    uint32_t x[32] = {};
    uint32_t pc = 0;
    // 0 user, 1 supervisor, 3 machine
    uint32_t priv = 3;
    bool waiting = false;

    // the time csr, kept by the owner (the clint's mtime)
    uint64_t time = 0;
    // counted per retired instruction, and per stall cycle by the owner
    uint64_t cycle = 0;
    uint64_t instret = 0;

    uint32_t hartid;
    uint32_t mstatus = 0;
    uint32_t medeleg = 0;
    uint32_t mideleg = 0;
    uint32_t mie = 0;
    uint32_t mtvec = 0;
    uint32_t mcounteren = 0;
    uint32_t mcountinhibit = 0;
    uint32_t mscratch = 0;
    uint32_t mepc = 0;
    uint32_t mcause = 0;
    uint32_t mtval = 0;
    uint32_t stvec = 0;
    uint32_t scounteren = 0;
    uint32_t sscratch = 0;
    uint32_t sepc = 0;
    uint32_t scause = 0;
    uint32_t stval = 0;
    uint32_t satp = 0;
    uint32_t tdata2 = 0;

private:
    enum class status {
        ok,
        trap,
        stall
    };

    status execute(const decoded& d);
    status raise(uint32_t cause, uint32_t tval);
    void take_trap(uint32_t cause, uint32_t tval);
    bool take_interrupt();

    status translate(uint32_t va, access kind, uint32_t& pa);
    status walk(uint32_t va, access kind, uint32_t level_priv, uint32_t& pa);
    status read(uint32_t va, int size, uint32_t& value, access kind);
    status write(uint32_t va, int size, uint32_t value);
    status fetch(uint32_t& inst);

    status csr_op(const decoded& d);
    bool csr_read(uint32_t csr, uint32_t& value) const;
    // false if the csr can't be written
    bool csr_write(uint32_t csr, uint32_t value);

    bus& m_bus;
    // the software writable bits of mip: ssip, stip and seip
    uint32_t m_mip = 0;
    uint32_t m_lines = 0;
    bool m_reserved = false;
    uint32_t m_reservation = 0;
    // set by an instruction that wrote the counter
    bool m_skip_cycle = false;
    bool m_skip_instret = false;
};

}
