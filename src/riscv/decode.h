#pragma once

// rv32ima_zicsr_zifencei instructions decoded once, so the hart and its
// instruction cache don't take the fields apart again on every execution

#include <cstdint>
#include <string>

namespace logic::riscv {

enum class op : uint8_t {
    illegal,
    lui,
    auipc,
    jal,
    jalr,
    beq,
    bne,
    blt,
    bge,
    bltu,
    bgeu,
    lb,
    lh,
    lw,
    lbu,
    lhu,
    sb,
    sh,
    sw,
    addi,
    slti,
    sltiu,
    xori,
    ori,
    andi,
    slli,
    srli,
    srai,
    add,
    sub,
    sll,
    slt,
    sltu,
    xor_,
    srl,
    sra,
    or_,
    and_,
    mul,
    mulh,
    mulhsu,
    mulhu,
    div,
    divu,
    rem,
    remu,
    fence,
    fence_i,
    ecall,
    ebreak,
    mret,
    sret,
    wfi,
    sfence_vma,
    csrrw,
    csrrs,
    csrrc,
    csrrwi,
    csrrsi,
    csrrci,
    lr_w,
    sc_w,
    amoswap_w,
    amoadd_w,
    amoxor_w,
    amoand_w,
    amoor_w,
    amomin_w,
    amomax_w,
    amominu_w,
    amomaxu_w,
};

struct decoded {
    op o = op::illegal;
    uint8_t rd = 0;
    // the zimm of csrr*i
    uint8_t rs1 = 0;
    uint8_t rs2 = 0;
    // the immediate, shift amount or csr number
    int32_t imm = 0;
    uint32_t raw = 0;
};

decoded decode(uint32_t inst);

// "addi a0, a0, 1", branch and jump targets as addresses from pc
std::string disassemble(uint32_t inst, uint32_t pc);
// the name of a csr, "" if it has none here
const char* csr_name(uint32_t csr);

}
