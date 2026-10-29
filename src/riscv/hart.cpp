// after riscv-simulator by hdinc and uwrk9amtdn
// (github.com/uwrk9amtdn/riscv-simulator, smode branch)

#include "hart.h"

namespace logic::riscv {

namespace {

namespace ms {
constexpr uint32_t sie = 1u << 1;
constexpr uint32_t mie = 1u << 3;
constexpr uint32_t spie = 1u << 5;
constexpr uint32_t mpie = 1u << 7;
constexpr uint32_t spp = 1u << 8;
constexpr uint32_t mpp = 3u << 11;
constexpr uint32_t mprv = 1u << 17;
constexpr uint32_t sum = 1u << 18;
constexpr uint32_t mxr = 1u << 19;
constexpr uint32_t tvm = 1u << 20;
constexpr uint32_t tw = 1u << 21;
constexpr uint32_t tsr = 1u << 22;
constexpr uint32_t writable = sie | mie | spie | mpie | spp | mpp | mprv | sum | mxr | tvm | tw | tsr;
constexpr uint32_t sstatus = sie | spie | spp | sum | mxr;
}

// MXL 32, extensions A, I, M, S and U
constexpr uint32_t misa = 0x40141101;

namespace cause {
constexpr uint32_t fetch_misaligned = 0;
constexpr uint32_t fetch_fault = 1;
constexpr uint32_t illegal = 2;
constexpr uint32_t breakpoint = 3;
constexpr uint32_t load_misaligned = 4;
constexpr uint32_t load_fault = 5;
constexpr uint32_t store_misaligned = 6;
constexpr uint32_t store_fault = 7;
constexpr uint32_t ecall_u = 8;
constexpr uint32_t fetch_page = 12;
constexpr uint32_t load_page = 13;
constexpr uint32_t store_page = 15;
constexpr uint32_t interrupt = 1u << 31;
}

uint32_t access_fault(access kind) { return kind == access::execute ? cause::fetch_fault : kind == access::write ? cause::store_fault : cause::load_fault; }
uint32_t page_fault(access kind) { return kind == access::execute ? cause::fetch_page : kind == access::write ? cause::store_page : cause::load_page; }
uint32_t misaligned(access kind) { return kind == access::execute ? cause::fetch_misaligned : kind == access::write ? cause::store_misaligned : cause::load_misaligned; }

// the order the spec takes simultaneous interrupts in
constexpr uint32_t interrupt_order[] = {11, 3, 7, 9, 1, 5};

}

hart::hart(bus& memory, uint32_t hartid)
    : hartid(hartid)
    , m_bus(memory)
{
    reset(0);
}

void hart::reset(uint32_t start)
{
    for (auto& r : x) r = 0;
    pc = start;
    priv = 3;
    waiting = false;
    cycle = instret = 0;
    mstatus = ms::mpp;
    medeleg = mideleg = mie = mtvec = mcounteren = mcountinhibit = 0;
    mscratch = mepc = mcause = mtval = 0;
    stvec = scounteren = sscratch = sepc = scause = stval = satp = 0;
    tdata2 = 0;
    tlb_misses = 0;
    flush_tlb();
    m_mip = 0;
    m_reserved = false;
    m_retry = false;
}

step_result hart::step()
{
    m_skip_cycle = m_skip_instret = false;
    status s;
    if (!m_retry && take_interrupt()) {
        s = status::trap;
    } else if (waiting) {
        return step_result::waiting;
    } else {
        uint32_t inst = 0;
        s = fetch(inst);
        if (s == status::ok) s = execute(decode(inst));
        m_retry = s == status::stall;
        if (m_retry) return step_result::stalled;
        x[0] = 0;
    }
    if (!(mcountinhibit & 1) && !m_skip_cycle) cycle++;
    if (s == status::trap) return step_result::trapped;
    if (!(mcountinhibit & 4) && !m_skip_instret) instret++;
    return step_result::retired;
}

bool hart::take_interrupt()
{
    uint32_t pending = mip() & mie;
    if (!pending) return false;
    waiting = false;
    uint32_t m = pending & ~mideleg;
    uint32_t s = pending & mideleg;
    if (priv == 3 && !(mstatus & ms::mie)) m = 0;
    if (priv == 3 || (priv == 1 && !(mstatus & ms::sie))) s = 0;
    // machine level interrupts go first
    uint32_t take = m ? m : s;
    if (!take) return false;
    for (uint32_t code : interrupt_order)
        if (take & (1u << code)) {
            take_trap(cause::interrupt | code, 0);
            return true;
        }
    return false;
}

hart::status hart::raise(uint32_t c, uint32_t tval)
{
    take_trap(c, tval);
    return status::trap;
}

void hart::take_trap(uint32_t c, uint32_t tval)
{
    bool intr = c & cause::interrupt;
    uint32_t code = c & 31;
    waiting = false;
    bool delegate = priv != 3 && (((intr ? mideleg : medeleg) >> code) & 1);
    if (delegate) {
        scause = c;
        stval = tval;
        sepc = pc;
        mstatus = (mstatus & ~(ms::spie | ms::sie | ms::spp)) | (mstatus & ms::sie ? ms::spie : 0) | (priv ? ms::spp : 0);
        priv = 1;
        pc = (stvec & ~3u) + (intr && (stvec & 3) == 1 ? 4 * code : 0);
    } else {
        mcause = c;
        mtval = tval;
        mepc = pc;
        mstatus = (mstatus & ~(ms::mpie | ms::mie | ms::mpp)) | (mstatus & ms::mie ? ms::mpie : 0) | (priv << 11);
        priv = 3;
        pc = (mtvec & ~3u) + (intr && (mtvec & 3) == 1 ? 4 * code : 0);
    }
}

hart::status hart::translate(uint32_t va, access kind, uint32_t& pa)
{
    uint32_t p = priv;
    if (kind != access::execute && (mstatus & ms::mprv)) p = (mstatus >> 11) & 3;
    if (p == 3 || !(satp >> 31)) {
        pa = va;
        return status::ok;
    }
    auto& e = m_tlb[(va >> 12) % tlb_size];
    if (e.vpn == va >> 12 && (kind != access::write || (e.pte & 0x80))) {
        auto s = check_leaf(e.pte, e.level, va, kind, p);
        if (s != status::ok) return s;
        pa = e.level == 1 ? ((e.pte >> 20) << 22) | (va & 0x3FFFFF) : ((e.pte >> 10) << 12) | (va & 0xFFF);
        return status::ok;
    }
    tlb_misses++;
    return walk(va, kind, p, pa);
}

void hart::flush_tlb()
{
    for (auto& e : m_tlb) e = {};
}

std::optional<uint32_t> hart::debug_translate(uint32_t va) const
{
    if (priv == 3 || !(satp >> 31)) return va;
    auto& e = m_tlb[(va >> 12) % tlb_size];
    if (e.vpn != va >> 12) return std::nullopt;
    return e.level == 1 ? ((e.pte >> 20) << 22) | (va & 0x3FFFFF) : ((e.pte >> 10) << 12) | (va & 0xFFF);
}

hart::status hart::check_leaf(uint32_t pte, int level, uint32_t va, access kind, uint32_t p)
{
    bool user = pte & 0x10;
    if (p == 1 && user && (kind == access::execute || !(mstatus & ms::sum))) return raise(page_fault(kind), va);
    if (p == 0 && !user) return raise(page_fault(kind), va);
    bool allowed = kind == access::execute ? (pte & 8) : kind == access::write ? (pte & 4) : ((pte & 2) || ((mstatus & ms::mxr) && (pte & 8)));
    if (!allowed) return raise(page_fault(kind), va);
    // a misaligned superpage
    if (level == 1 && ((pte >> 10) & 0x3FF)) return raise(page_fault(kind), va);
    return status::ok;
}

hart::status hart::walk(uint32_t va, access kind, uint32_t p, uint32_t& pa)
{
    uint64_t table = uint64_t(satp & 0x3FFFFF) << 12;
    for (int level = 1; level >= 0; level--) {
        uint64_t address = table + ((va >> (12 + 10 * level)) & 0x3FF) * 4;
        if (address >> 32) return raise(access_fault(kind), va);
        uint32_t pte = 0;
        switch (m_bus.load(uint32_t(address), 4, pte, access::read)) {
        case result::ok: break;
        case result::stall: return status::stall;
        default: return raise(access_fault(kind), va);
        }
        // invalid, or writable without being readable
        if (!(pte & 1) || (pte & 6) == 4) return raise(page_fault(kind), va);
        if (!(pte & 0xA)) {
            // a pointer to the next level, its d, a and u bits are reserved
            if (level == 0 || (pte & 0xD0)) return raise(page_fault(kind), va);
            table = uint64_t(pte >> 10) << 12;
            continue;
        }
        auto s = check_leaf(pte, level, va, kind, p);
        if (s != status::ok) return s;
        // the accessed and dirty bits are set by the hardware. the single
        // master needs no atomic update
        uint32_t updated = pte | 0x40 | (kind == access::write ? 0x80 : 0);
        if (updated != pte) {
            switch (m_bus.store(uint32_t(address), 4, updated)) {
            case result::ok: break;
            case result::stall: return status::stall;
            default: return raise(access_fault(kind), va);
            }
        }
        uint64_t physical = level == 1 ? (uint64_t(pte >> 20) << 22) | (va & 0x3FFFFF) : (uint64_t(pte >> 10) << 12) | (va & 0xFFF);
        if (physical >> 32) return raise(access_fault(kind), va);
        // later accesses to the page don't walk, so retrying an instruction
        // that stalls on its data needs only the data's cache line
        m_tlb[(va >> 12) % tlb_size] = {va >> 12, updated, level};
        pa = uint32_t(physical);
        return status::ok;
    }
    return raise(page_fault(kind), va);
}

hart::status hart::read(uint32_t va, int size, uint32_t& value, access kind)
{
    // an access that crosses a page is split, its parts are translated
    // separately
    uint32_t in_page = 0x1000 - (va & 0xFFF);
    int first = size <= int(in_page) ? size : int(in_page);
    value = 0;
    for (int done = 0; done < size;) {
        int n = done ? size - done : first;
        uint32_t pa = 0;
        auto s = translate(va + done, kind, pa);
        if (s != status::ok) return s;
        uint32_t part = 0;
        switch (m_bus.load(pa, n, part, kind)) {
        case result::ok: break;
        case result::stall: return status::stall;
        case result::misaligned: return raise(misaligned(kind), va);
        default: return raise(access_fault(kind), va);
        }
        value |= part << (8 * done);
        done += n;
    }
    return status::ok;
}

hart::status hart::write(uint32_t va, int size, uint32_t value)
{
    uint32_t in_page = 0x1000 - (va & 0xFFF);
    int first = size <= int(in_page) ? size : int(in_page);
    // both parts are translated before anything is stored
    uint32_t pa[2] = {};
    int count = first < size ? 2 : 1;
    for (int k = 0; k < count; k++) {
        auto s = translate(va + uint32_t(k ? first : 0), access::write, pa[k]);
        if (s != status::ok) return s;
    }
    for (int k = 0; k < count; k++) {
        int n = k ? size - first : first;
        uint32_t part = k ? value >> (8 * first) : value;
        switch (m_bus.store(pa[k], n, part)) {
        case result::ok: break;
        case result::stall: return status::stall;
        case result::misaligned: return raise(cause::store_misaligned, va);
        default: return raise(cause::store_fault, va);
        }
    }
    return status::ok;
}

hart::status hart::fetch(uint32_t& inst)
{
    uint32_t pa = 0;
    auto s = translate(pc, access::execute, pa);
    if (s != status::ok) return s;
    switch (m_bus.load(pa, 4, inst, access::execute)) {
    case result::ok: return status::ok;
    case result::stall: return status::stall;
    default: return raise(cause::fetch_fault, pc);
    }
}

hart::status hart::execute(const decoded& d)
{
    uint32_t a = x[d.rs1];
    uint32_t b = x[d.rs2];
    uint32_t imm = uint32_t(d.imm);
    uint32_t next = pc + 4;
    auto jump = [&](uint32_t target) {
        if (target & 3) return raise(cause::fetch_misaligned, target);
        x[d.rd] = next;
        pc = target;
        return status::ok;
    };
    auto branch = [&](bool taken) {
        if (!taken) {
            pc = next;
            return status::ok;
        }
        uint32_t target = pc + imm;
        if (target & 3) return raise(cause::fetch_misaligned, target);
        pc = target;
        return status::ok;
    };
    auto load = [&](int size, bool sign) {
        uint32_t v = 0;
        auto s = read(a + imm, size, v, access::read);
        if (s != status::ok) return s;
        if (sign) v = size == 1 ? uint32_t(int32_t(int8_t(v))) : size == 2 ? uint32_t(int32_t(int16_t(v))) : v;
        x[d.rd] = v;
        pc = next;
        return status::ok;
    };
    auto store = [&](int size) {
        auto s = write(a + imm, size, size == 4 ? b : b & ((1u << (8 * size)) - 1));
        if (s != status::ok) return s;
        pc = next;
        return status::ok;
    };
    auto result_rd = [&](uint32_t v) {
        x[d.rd] = v;
        pc = next;
        return status::ok;
    };
    auto amo = [&](auto f) {
        if (a & 3) return raise(cause::store_misaligned, a);
        uint32_t pa = 0;
        auto s = translate(a, access::write, pa);
        if (s != status::ok) return s;
        uint32_t old = 0;
        switch (m_bus.load(pa, 4, old, access::write)) {
        case result::ok: break;
        case result::stall: return status::stall;
        case result::misaligned: return raise(cause::store_misaligned, a);
        default: return raise(cause::store_fault, a);
        }
        switch (m_bus.store(pa, 4, f(old, b))) {
        case result::ok: break;
        case result::stall: return status::stall;
        case result::misaligned: return raise(cause::store_misaligned, a);
        default: return raise(cause::store_fault, a);
        }
        return result_rd(old);
    };

    switch (d.o) {
    case op::illegal: return raise(cause::illegal, d.raw);
    case op::lui: return result_rd(imm);
    case op::auipc: return result_rd(pc + imm);
    case op::jal: return jump(pc + imm);
    case op::jalr: return jump((a + imm) & ~1u);
    case op::beq: return branch(a == b);
    case op::bne: return branch(a != b);
    case op::blt: return branch(int32_t(a) < int32_t(b));
    case op::bge: return branch(int32_t(a) >= int32_t(b));
    case op::bltu: return branch(a < b);
    case op::bgeu: return branch(a >= b);
    case op::lb: return load(1, true);
    case op::lh: return load(2, true);
    case op::lw: return load(4, false);
    case op::lbu: return load(1, false);
    case op::lhu: return load(2, false);
    case op::sb: return store(1);
    case op::sh: return store(2);
    case op::sw: return store(4);
    case op::addi: return result_rd(a + imm);
    case op::slti: return result_rd(int32_t(a) < int32_t(imm));
    case op::sltiu: return result_rd(a < imm);
    case op::xori: return result_rd(a ^ imm);
    case op::ori: return result_rd(a | imm);
    case op::andi: return result_rd(a & imm);
    case op::slli: return result_rd(a << imm);
    case op::srli: return result_rd(a >> imm);
    case op::srai: return result_rd(uint32_t(int32_t(a) >> imm));
    case op::add: return result_rd(a + b);
    case op::sub: return result_rd(a - b);
    case op::sll: return result_rd(a << (b & 31));
    case op::slt: return result_rd(int32_t(a) < int32_t(b));
    case op::sltu: return result_rd(a < b);
    case op::xor_: return result_rd(a ^ b);
    case op::srl: return result_rd(a >> (b & 31));
    case op::sra: return result_rd(uint32_t(int32_t(a) >> (b & 31)));
    case op::or_: return result_rd(a | b);
    case op::and_: return result_rd(a & b);
    case op::mul: return result_rd(a * b);
    case op::mulh: return result_rd(uint32_t(uint64_t(int64_t(int32_t(a)) * int64_t(int32_t(b))) >> 32));
    case op::mulhsu: return result_rd(uint32_t(uint64_t(int64_t(int32_t(a)) * int64_t(uint64_t(b))) >> 32));
    case op::mulhu: return result_rd(uint32_t((uint64_t(a) * uint64_t(b)) >> 32));
    case op::div:
        if (b == 0) return result_rd(~0u);
        if (a == 0x80000000u && b == ~0u) return result_rd(a);
        return result_rd(uint32_t(int32_t(a) / int32_t(b)));
    case op::divu: return result_rd(b == 0 ? ~0u : a / b);
    case op::rem:
        if (b == 0) return result_rd(a);
        if (a == 0x80000000u && b == ~0u) return result_rd(0);
        return result_rd(uint32_t(int32_t(a) % int32_t(b)));
    case op::remu: return result_rd(b == 0 ? a : a % b);
    case op::fence: pc = next; return status::ok;
    case op::fence_i:
        m_bus.fence_i();
        pc = next;
        return status::ok;
    case op::ecall: return raise(cause::ecall_u + priv, 0);
    case op::ebreak: return raise(cause::breakpoint, pc);
    case op::mret: {
        if (priv != 3) return raise(cause::illegal, d.raw);
        uint32_t to = (mstatus >> 11) & 3;
        mstatus = (mstatus & ~(ms::mie | ms::mpp)) | (mstatus & ms::mpie ? ms::mie : 0) | ms::mpie;
        if (to != 3) mstatus &= ~ms::mprv;
        priv = to;
        pc = mepc;
        return status::ok;
    }
    case op::sret: {
        if (priv == 0 || (priv == 1 && (mstatus & ms::tsr))) return raise(cause::illegal, d.raw);
        uint32_t to = (mstatus & ms::spp) ? 1 : 0;
        mstatus = (mstatus & ~(ms::sie | ms::spp | ms::mprv)) | (mstatus & ms::spie ? ms::sie : 0) | ms::spie;
        priv = to;
        pc = sepc;
        return status::ok;
    }
    case op::wfi:
        if (priv == 0 || (priv == 1 && (mstatus & ms::tw))) return raise(cause::illegal, d.raw);
        waiting = true;
        pc = next;
        return status::ok;
    case op::sfence_vma:
        if (priv == 0 || (priv == 1 && (mstatus & ms::tvm))) return raise(cause::illegal, d.raw);
        flush_tlb();
        pc = next;
        return status::ok;
    case op::csrrw:
    case op::csrrs:
    case op::csrrc:
    case op::csrrwi:
    case op::csrrsi:
    case op::csrrci: return csr_op(d);
    case op::lr_w: {
        if (a & 3) return raise(cause::load_misaligned, a);
        uint32_t pa = 0;
        auto s = translate(a, access::read, pa);
        if (s != status::ok) return s;
        uint32_t v = 0;
        switch (m_bus.load(pa, 4, v, access::read)) {
        case result::ok: break;
        case result::stall: return status::stall;
        case result::misaligned: return raise(cause::load_misaligned, a);
        default: return raise(cause::load_fault, a);
        }
        m_reserved = true;
        m_reservation = pa;
        return result_rd(v);
    }
    case op::sc_w: {
        if (a & 3) return raise(cause::store_misaligned, a);
        uint32_t pa = 0;
        auto s = translate(a, access::write, pa);
        if (s != status::ok) return s;
        if (!m_reserved || m_reservation != pa) return result_rd(1);
        switch (m_bus.store(pa, 4, b)) {
        case result::ok: break;
        case result::stall: return status::stall;
        case result::misaligned: return raise(cause::store_misaligned, a);
        default: return raise(cause::store_fault, a);
        }
        m_reserved = false;
        return result_rd(0);
    }
    case op::amoswap_w: return amo([](uint32_t, uint32_t v) { return v; });
    case op::amoadd_w: return amo([](uint32_t o, uint32_t v) { return o + v; });
    case op::amoxor_w: return amo([](uint32_t o, uint32_t v) { return o ^ v; });
    case op::amoand_w: return amo([](uint32_t o, uint32_t v) { return o & v; });
    case op::amoor_w: return amo([](uint32_t o, uint32_t v) { return o | v; });
    case op::amomin_w: return amo([](uint32_t o, uint32_t v) { return int32_t(o) < int32_t(v) ? o : v; });
    case op::amomax_w: return amo([](uint32_t o, uint32_t v) { return int32_t(o) > int32_t(v) ? o : v; });
    case op::amominu_w: return amo([](uint32_t o, uint32_t v) { return o < v ? o : v; });
    case op::amomaxu_w: return amo([](uint32_t o, uint32_t v) { return o > v ? o : v; });
    }
    return raise(cause::illegal, d.raw);
}

hart::status hart::csr_op(const decoded& d)
{
    uint32_t csr = uint32_t(d.imm) & 0xFFF;
    bool immediate = d.o == op::csrrwi || d.o == op::csrrsi || d.o == op::csrrci;
    bool swap = d.o == op::csrrw || d.o == op::csrrwi;
    uint32_t src = immediate ? d.rs1 : x[d.rs1];
    // csrrs and csrrc with x0 or 0 don't write, so they work on read only csrs
    bool writes = swap || d.rs1 != 0;
    if (priv < ((csr >> 8) & 3)) return raise(cause::illegal, d.raw);
    if (writes && (csr >> 10) == 3) return raise(cause::illegal, d.raw);
    if (csr == 0x180 && priv == 1 && (mstatus & ms::tvm)) return raise(cause::illegal, d.raw);
    // the user counters need mcounteren below machine mode, and scounteren
    // in user mode
    if ((csr >= 0xC00 && csr <= 0xC1F) || (csr >= 0xC80 && csr <= 0xC9F)) {
        uint32_t bit = 1u << (csr & 31);
        if ((priv < 3 && !(mcounteren & bit)) || (priv == 0 && !(scounteren & bit))) return raise(cause::illegal, d.raw);
    }
    uint32_t old = 0;
    if (!csr_read(csr, old)) return raise(cause::illegal, d.raw);
    if (writes) {
        uint32_t v = swap ? src : d.o == op::csrrs || d.o == op::csrrsi ? old | src : old & ~src;
        if (!csr_write(csr, v)) return raise(cause::illegal, d.raw);
    }
    x[d.rd] = old;
    pc += 4;
    return status::ok;
}

std::optional<uint32_t> hart::read_csr(uint32_t csr) const
{
    uint32_t v = 0;
    if (!csr_read(csr, v)) return std::nullopt;
    return v;
}

bool hart::csr_read(uint32_t csr, uint32_t& v) const
{
    // hardware performance counters and their events read as zero
    if ((csr >= 0xB03 && csr <= 0xB1F) || (csr >= 0xB83 && csr <= 0xB9F) || (csr >= 0xC03 && csr <= 0xC1F) || (csr >= 0xC83 && csr <= 0xC9F) ||
        (csr >= 0x323 && csr <= 0x33F)) {
        v = 0;
        return true;
    }
    // no pmp entries: the csrs are there and read as zero
    if (csr >= 0x3A0 && csr <= 0x3EF) {
        v = 0;
        return true;
    }
    switch (csr) {
    case 0x100: v = mstatus & ms::sstatus; return true;
    case 0x104: v = mie & mideleg; return true;
    case 0x105: v = stvec; return true;
    case 0x106: v = scounteren; return true;
    case 0x140: v = sscratch; return true;
    case 0x141: v = sepc; return true;
    case 0x142: v = scause; return true;
    case 0x143: v = stval; return true;
    case 0x144: v = mip() & mideleg; return true;
    case 0x180: v = satp; return true;
    case 0x300: v = mstatus; return true;
    case 0x301: v = misa; return true;
    case 0x302: v = medeleg; return true;
    case 0x303: v = mideleg; return true;
    case 0x304: v = mie; return true;
    case 0x305: v = mtvec; return true;
    case 0x306: v = mcounteren; return true;
    case 0x310: v = 0; return true;
    case 0x320: v = mcountinhibit; return true;
    case 0x340: v = mscratch; return true;
    case 0x341: v = mepc; return true;
    case 0x342: v = mcause; return true;
    case 0x343: v = mtval; return true;
    case 0x344: v = mip(); return true;
    // no triggers: tselect stays 0 and tdata1 reads as 0, so software sees
    // that its breakpoint type isn't supported
    case 0x7A0:
    case 0x7A1:
    case 0x7A3: v = 0; return true;
    case 0x7A2: v = tdata2; return true;
    case 0xB00:
    case 0xC00: v = uint32_t(cycle); return true;
    case 0xB80:
    case 0xC80: v = uint32_t(cycle >> 32); return true;
    case 0xB02:
    case 0xC02: v = uint32_t(instret); return true;
    case 0xB82:
    case 0xC82: v = uint32_t(instret >> 32); return true;
    case 0xC01: v = uint32_t(time); return true;
    case 0xC81: v = uint32_t(time >> 32); return true;
    case 0xF11:
    case 0xF12:
    case 0xF13:
    case 0xF15: v = 0; return true;
    case 0xF14: v = hartid; return true;
    default: return false;
    }
}

bool hart::csr_write(uint32_t csr, uint32_t v)
{
    if ((csr >= 0xB03 && csr <= 0xB1F) || (csr >= 0xB83 && csr <= 0xB9F) || (csr >= 0x323 && csr <= 0x33F) || (csr >= 0x3A0 && csr <= 0x3EF))
        return true;
    switch (csr) {
    case 0x100: mstatus = (mstatus & ~ms::sstatus) | (v & ms::sstatus); return true;
    case 0x104: mie = (mie & ~mideleg) | (v & mideleg); return true;
    case 0x105: stvec = v & ~2u; return true;
    case 0x106: scounteren = v & 7; return true;
    case 0x140: sscratch = v; return true;
    case 0x141: sepc = v & ~3u; return true;
    case 0x142: scause = v; return true;
    case 0x143: stval = v; return true;
    case 0x144: m_mip = (m_mip & ~(mideleg & ip::ssip)) | (v & mideleg & ip::ssip); return true;
    // no asids, and physical addresses stay below 4 GiB
    case 0x180:
        satp = v & 0x800FFFFFu;
        flush_tlb();
        return true;
    case 0x300:
        mstatus = (mstatus & ~ms::writable) | (v & ms::writable);
        // there is no hypervisor mode
        if (((mstatus >> 11) & 3) == 2) mstatus |= ms::mpp;
        return true;
    case 0x301: return true;
    // an ecall from machine mode can't be delegated
    case 0x302: medeleg = v & 0xB3FF; return true;
    case 0x303: mideleg = v & (ip::ssip | ip::stip | ip::seip); return true;
    case 0x304: mie = v & 0xAAA; return true;
    case 0x305: mtvec = v & ~2u; return true;
    case 0x306: mcounteren = v & 7; return true;
    case 0x310: return true;
    case 0x320: mcountinhibit = v & 5; return true;
    case 0x340: mscratch = v; return true;
    case 0x341: mepc = v & ~3u; return true;
    case 0x342: mcause = v; return true;
    case 0x343: mtval = v; return true;
    case 0x344: m_mip = (m_mip & ~(ip::ssip | ip::stip | ip::seip)) | (v & (ip::ssip | ip::stip | ip::seip)); return true;
    case 0x7A0:
    case 0x7A1:
    case 0x7A3: return true;
    case 0x7A2: tdata2 = v; return true;
    // a written counter isn't counted by the writing instruction
    case 0xB00:
        cycle = (cycle & ~0xFFFFFFFFull) | v;
        m_skip_cycle = true;
        return true;
    case 0xB80:
        cycle = (cycle & 0xFFFFFFFFull) | (uint64_t(v) << 32);
        m_skip_cycle = true;
        return true;
    case 0xB02:
        instret = (instret & ~0xFFFFFFFFull) | v;
        m_skip_instret = true;
        return true;
    case 0xB82:
        instret = (instret & 0xFFFFFFFFull) | (uint64_t(v) << 32);
        m_skip_instret = true;
        return true;
    default: return false;
    }
}

}
