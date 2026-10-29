// instruction decoding after riscv-simulator by hdinc and uwrk9amtdn
// (github.com/uwrk9amtdn/riscv-simulator, smode branch)

#include "decode.h"

#include <cstdio>

namespace logic::riscv {

namespace {

int32_t imm_i(uint32_t inst) { return int32_t(inst) >> 20; }
int32_t imm_s(uint32_t inst) { return (int32_t(inst) >> 25) << 5 | int32_t((inst >> 7) & 0x1F); }
int32_t imm_b(uint32_t inst)
{
    return (int32_t(inst & 0x80000000u) >> 19) | int32_t((inst & 0x80) << 4) | int32_t((inst >> 20) & 0x7E0) | int32_t((inst >> 7) & 0x1E);
}
int32_t imm_u(uint32_t inst) { return int32_t(inst & 0xFFFFF000u); }
int32_t imm_j(uint32_t inst)
{
    return (int32_t(inst & 0x80000000u) >> 11) | int32_t(inst & 0xFF000) | int32_t((inst >> 9) & 0x800) | int32_t((inst >> 20) & 0x7FE);
}

op decode_op(uint32_t inst)
{
    uint32_t f3 = (inst >> 12) & 7;
    uint32_t f7 = inst >> 25;
    switch (inst & 0x7F) {
    case 0x37: return op::lui;
    case 0x17: return op::auipc;
    case 0x6F: return op::jal;
    case 0x67: return f3 == 0 ? op::jalr : op::illegal;
    case 0x63: {
        static const op ops[8] = {op::beq, op::bne, op::illegal, op::illegal, op::blt, op::bge, op::bltu, op::bgeu};
        return ops[f3];
    }
    case 0x03: {
        static const op ops[8] = {op::lb, op::lh, op::lw, op::illegal, op::lbu, op::lhu, op::illegal, op::illegal};
        return ops[f3];
    }
    case 0x23: {
        static const op ops[8] = {op::sb, op::sh, op::sw, op::illegal, op::illegal, op::illegal, op::illegal, op::illegal};
        return ops[f3];
    }
    case 0x13:
        switch (f3) {
        case 0: return op::addi;
        case 1: return f7 == 0 ? op::slli : op::illegal;
        case 2: return op::slti;
        case 3: return op::sltiu;
        case 4: return op::xori;
        case 5: return f7 == 0 ? op::srli : f7 == 0x20 ? op::srai : op::illegal;
        case 6: return op::ori;
        default: return op::andi;
        }
    case 0x33:
        if (f7 == 0) {
            static const op ops[8] = {op::add, op::sll, op::slt, op::sltu, op::xor_, op::srl, op::or_, op::and_};
            return ops[f3];
        }
        if (f7 == 1) {
            static const op ops[8] = {op::mul, op::mulh, op::mulhsu, op::mulhu, op::div, op::divu, op::rem, op::remu};
            return ops[f3];
        }
        if (f7 == 0x20) return f3 == 0 ? op::sub : f3 == 5 ? op::sra : op::illegal;
        return op::illegal;
    case 0x0F: return f3 == 0 ? op::fence : f3 == 1 ? op::fence_i : op::illegal;
    case 0x73:
        switch (f3) {
        case 0:
            switch (inst) {
            case 0x00000073: return op::ecall;
            case 0x00100073: return op::ebreak;
            case 0x10200073: return op::sret;
            case 0x30200073: return op::mret;
            case 0x10500073: return op::wfi;
            default: return (inst & 0xFE007FFF) == 0x12000073 ? op::sfence_vma : op::illegal;
            }
        case 1: return op::csrrw;
        case 2: return op::csrrs;
        case 3: return op::csrrc;
        case 5: return op::csrrwi;
        case 6: return op::csrrsi;
        case 7: return op::csrrci;
        default: return op::illegal;
        }
    case 0x2F:
        if (f3 != 2) return op::illegal;
        switch (inst >> 27) {
        case 0x02: return ((inst >> 20) & 0x1F) == 0 ? op::lr_w : op::illegal;
        case 0x03: return op::sc_w;
        case 0x01: return op::amoswap_w;
        case 0x00: return op::amoadd_w;
        case 0x04: return op::amoxor_w;
        case 0x0C: return op::amoand_w;
        case 0x08: return op::amoor_w;
        case 0x10: return op::amomin_w;
        case 0x14: return op::amomax_w;
        case 0x18: return op::amominu_w;
        case 0x1C: return op::amomaxu_w;
        default: return op::illegal;
        }
    default: return op::illegal;
    }
}

const char* const reg_names[32] = {"zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};

const char* op_name(op o)
{
    switch (o) {
    case op::illegal: return "illegal";
    case op::lui: return "lui";
    case op::auipc: return "auipc";
    case op::jal: return "jal";
    case op::jalr: return "jalr";
    case op::beq: return "beq";
    case op::bne: return "bne";
    case op::blt: return "blt";
    case op::bge: return "bge";
    case op::bltu: return "bltu";
    case op::bgeu: return "bgeu";
    case op::lb: return "lb";
    case op::lh: return "lh";
    case op::lw: return "lw";
    case op::lbu: return "lbu";
    case op::lhu: return "lhu";
    case op::sb: return "sb";
    case op::sh: return "sh";
    case op::sw: return "sw";
    case op::addi: return "addi";
    case op::slti: return "slti";
    case op::sltiu: return "sltiu";
    case op::xori: return "xori";
    case op::ori: return "ori";
    case op::andi: return "andi";
    case op::slli: return "slli";
    case op::srli: return "srli";
    case op::srai: return "srai";
    case op::add: return "add";
    case op::sub: return "sub";
    case op::sll: return "sll";
    case op::slt: return "slt";
    case op::sltu: return "sltu";
    case op::xor_: return "xor";
    case op::srl: return "srl";
    case op::sra: return "sra";
    case op::or_: return "or";
    case op::and_: return "and";
    case op::mul: return "mul";
    case op::mulh: return "mulh";
    case op::mulhsu: return "mulhsu";
    case op::mulhu: return "mulhu";
    case op::div: return "div";
    case op::divu: return "divu";
    case op::rem: return "rem";
    case op::remu: return "remu";
    case op::fence: return "fence";
    case op::fence_i: return "fence.i";
    case op::ecall: return "ecall";
    case op::ebreak: return "ebreak";
    case op::mret: return "mret";
    case op::sret: return "sret";
    case op::wfi: return "wfi";
    case op::sfence_vma: return "sfence.vma";
    case op::csrrw: return "csrrw";
    case op::csrrs: return "csrrs";
    case op::csrrc: return "csrrc";
    case op::csrrwi: return "csrrwi";
    case op::csrrsi: return "csrrsi";
    case op::csrrci: return "csrrci";
    case op::lr_w: return "lr.w";
    case op::sc_w: return "sc.w";
    case op::amoswap_w: return "amoswap.w";
    case op::amoadd_w: return "amoadd.w";
    case op::amoxor_w: return "amoxor.w";
    case op::amoand_w: return "amoand.w";
    case op::amoor_w: return "amoor.w";
    case op::amomin_w: return "amomin.w";
    case op::amomax_w: return "amomax.w";
    case op::amominu_w: return "amominu.w";
    case op::amomaxu_w: return "amomaxu.w";
    }
    return "?";
}

}

decoded decode(uint32_t inst)
{
    decoded d;
    d.raw = inst;
    d.o = decode_op(inst);
    d.rd = uint8_t((inst >> 7) & 0x1F);
    d.rs1 = uint8_t((inst >> 15) & 0x1F);
    d.rs2 = uint8_t((inst >> 20) & 0x1F);
    switch (inst & 0x7F) {
    case 0x37:
    case 0x17: d.imm = imm_u(inst); break;
    case 0x6F: d.imm = imm_j(inst); break;
    case 0x63: d.imm = imm_b(inst); break;
    case 0x23: d.imm = imm_s(inst); break;
    case 0x73: d.imm = int32_t(inst >> 20); break;
    default: d.imm = imm_i(inst); break;
    }
    if (d.o == op::slli || d.o == op::srli || d.o == op::srai) d.imm = d.rs2;
    return d;
}

const char* csr_name(uint32_t csr)
{
    switch (csr) {
    case 0x001: return "fflags";
    case 0x100: return "sstatus";
    case 0x104: return "sie";
    case 0x105: return "stvec";
    case 0x106: return "scounteren";
    case 0x10A: return "senvcfg";
    case 0x140: return "sscratch";
    case 0x141: return "sepc";
    case 0x142: return "scause";
    case 0x143: return "stval";
    case 0x144: return "sip";
    case 0x180: return "satp";
    case 0x300: return "mstatus";
    case 0x301: return "misa";
    case 0x302: return "medeleg";
    case 0x303: return "mideleg";
    case 0x304: return "mie";
    case 0x305: return "mtvec";
    case 0x306: return "mcounteren";
    case 0x310: return "mstatush";
    case 0x320: return "mcountinhibit";
    case 0x340: return "mscratch";
    case 0x341: return "mepc";
    case 0x342: return "mcause";
    case 0x343: return "mtval";
    case 0x344: return "mip";
    case 0x3A0: return "pmpcfg0";
    case 0x3B0: return "pmpaddr0";
    case 0x7A0: return "tselect";
    case 0x7A1: return "tdata1";
    case 0x7A2: return "tdata2";
    case 0xB00: return "mcycle";
    case 0xB02: return "minstret";
    case 0xB80: return "mcycleh";
    case 0xB82: return "minstreth";
    case 0xC00: return "cycle";
    case 0xC01: return "time";
    case 0xC02: return "instret";
    case 0xC80: return "cycleh";
    case 0xC81: return "timeh";
    case 0xC82: return "instreth";
    case 0xF11: return "mvendorid";
    case 0xF12: return "marchid";
    case 0xF13: return "mimpid";
    case 0xF14: return "mhartid";
    case 0xF15: return "mconfigptr";
    default: return "";
    }
}

std::string disassemble(uint32_t inst, uint32_t pc)
{
    auto d = decode(inst);
    const char* n = op_name(d.o);
    const char* rd = reg_names[d.rd];
    const char* rs1 = reg_names[d.rs1];
    const char* rs2 = reg_names[d.rs2];
    char buf[96];
    auto csr = [&]() {
        static thread_local char c[16];
        const char* name = csr_name(uint32_t(d.imm));
        if (*name) return name;
        std::snprintf(c, sizeof(c), "0x%03x", unsigned(d.imm));
        return (const char*)c;
    };
    switch (d.o) {
    case op::illegal: std::snprintf(buf, sizeof(buf), ".word 0x%08x", inst); break;
    case op::lui:
    case op::auipc: std::snprintf(buf, sizeof(buf), "%s %s, 0x%x", n, rd, uint32_t(d.imm) >> 12); break;
    case op::jal: std::snprintf(buf, sizeof(buf), "%s %s, 0x%x", n, rd, pc + uint32_t(d.imm)); break;
    case op::jalr:
    case op::lb:
    case op::lh:
    case op::lw:
    case op::lbu:
    case op::lhu: std::snprintf(buf, sizeof(buf), "%s %s, %d(%s)", n, rd, d.imm, rs1); break;
    case op::beq:
    case op::bne:
    case op::blt:
    case op::bge:
    case op::bltu:
    case op::bgeu: std::snprintf(buf, sizeof(buf), "%s %s, %s, 0x%x", n, rs1, rs2, pc + uint32_t(d.imm)); break;
    case op::sb:
    case op::sh:
    case op::sw: std::snprintf(buf, sizeof(buf), "%s %s, %d(%s)", n, rs2, d.imm, rs1); break;
    case op::addi:
    case op::slti:
    case op::sltiu:
    case op::xori:
    case op::ori:
    case op::andi:
    case op::slli:
    case op::srli:
    case op::srai: std::snprintf(buf, sizeof(buf), "%s %s, %s, %d", n, rd, rs1, d.imm); break;
    case op::fence:
    case op::fence_i:
    case op::ecall:
    case op::ebreak:
    case op::mret:
    case op::sret:
    case op::wfi: std::snprintf(buf, sizeof(buf), "%s", n); break;
    case op::sfence_vma: std::snprintf(buf, sizeof(buf), "%s %s, %s", n, rs1, rs2); break;
    case op::csrrw:
    case op::csrrs:
    case op::csrrc: std::snprintf(buf, sizeof(buf), "%s %s, %s, %s", n, rd, csr(), rs1); break;
    case op::csrrwi:
    case op::csrrsi:
    case op::csrrci: std::snprintf(buf, sizeof(buf), "%s %s, %s, %d", n, rd, csr(), d.rs1); break;
    case op::lr_w: std::snprintf(buf, sizeof(buf), "%s %s, (%s)", n, rd, rs1); break;
    case op::sc_w:
    case op::amoswap_w:
    case op::amoadd_w:
    case op::amoxor_w:
    case op::amoand_w:
    case op::amoor_w:
    case op::amomin_w:
    case op::amomax_w:
    case op::amominu_w:
    case op::amomaxu_w: std::snprintf(buf, sizeof(buf), "%s %s, %s, (%s)", n, rd, rs2, rs1); break;
    default: std::snprintf(buf, sizeof(buf), "%s %s, %s, %s", n, rd, rs1, rs2); break;
    }
    return buf;
}

}
