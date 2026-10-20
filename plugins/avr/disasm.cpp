// avr instruction set (avr, avr5 and xmega additions), table driven

#include "disasm.h"

#include <cstdio>

namespace avr {

namespace {

enum format {
    none,     // nop
    rd_rr,    // add r1, r2
    rd,       // inc r1
    rd_k8,    // ldi r16, 0x10 (r16..r31)
    rd4_rr4,  // muls r16, r17
    rd3_rr3,  // mulsu r16, r17 (r16..r23)
    movw,     // movw r24, r22
    word_k6,  // adiw r24, 0x01
    io_bit,   // sbi 0x05, 5
    in,       // in r24, 0x3f
    out,      // out 0x3f, r24
    rel12,    // rjmp
    rel7,     // brne
    rd_bit,   // bld r24, 3
    abs22,    // jmp 0x1234 (32 bit)
    lds,      // lds r24, 0x0100 (32 bit)
    sts,      // sts 0x0100, r24 (32 bit)
    ld_ptr,   // ld r24, X+ : the pointer text is in the table
    st_ptr,   // st X+, r24
    ldd,      // ldd r24, Y+3
    std_,     // std Y+3, r24
    des,      // des 0x03
    text_only // the table text is the whole instruction
};

struct op {
    uint16_t mask, match;
    const char* name;
    format f;
    const char* ptr = nullptr;
};

const op ops[] = {
    {0xFFFF, 0x0000, "nop", none},
    {0xFF00, 0x0100, "movw", movw},
    {0xFF00, 0x0200, "muls", rd4_rr4},
    {0xFF88, 0x0300, "mulsu", rd3_rr3},
    {0xFF88, 0x0308, "fmul", rd3_rr3},
    {0xFF88, 0x0380, "fmuls", rd3_rr3},
    {0xFF88, 0x0388, "fmulsu", rd3_rr3},
    {0xFC00, 0x0400, "cpc", rd_rr},
    {0xFC00, 0x0800, "sbc", rd_rr},
    {0xFC00, 0x0C00, "add", rd_rr},
    {0xFC00, 0x1000, "cpse", rd_rr},
    {0xFC00, 0x1400, "cp", rd_rr},
    {0xFC00, 0x1800, "sub", rd_rr},
    {0xFC00, 0x1C00, "adc", rd_rr},
    {0xFC00, 0x2000, "and", rd_rr},
    {0xFC00, 0x2400, "eor", rd_rr},
    {0xFC00, 0x2800, "or", rd_rr},
    {0xFC00, 0x2C00, "mov", rd_rr},
    {0xF000, 0x3000, "cpi", rd_k8},
    {0xF000, 0x4000, "sbci", rd_k8},
    {0xF000, 0x5000, "subi", rd_k8},
    {0xF000, 0x6000, "ori", rd_k8},
    {0xF000, 0x7000, "andi", rd_k8},

    // ld/st through Y and Z with displacement 0 are plain ld/st
    {0xFE0F, 0x8000, "ld", ld_ptr, "Z"},
    {0xFE0F, 0x8008, "ld", ld_ptr, "Y"},
    {0xFE0F, 0x8200, "st", st_ptr, "Z"},
    {0xFE0F, 0x8208, "st", st_ptr, "Y"},
    {0xD208, 0x8000, "ldd", ldd, "Z"},
    {0xD208, 0x8008, "ldd", ldd, "Y"},
    {0xD208, 0x8200, "std", std_, "Z"},
    {0xD208, 0x8208, "std", std_, "Y"},

    {0xFE0F, 0x9000, "lds", lds},
    {0xFE0F, 0x9001, "ld", ld_ptr, "Z+"},
    {0xFE0F, 0x9002, "ld", ld_ptr, "-Z"},
    {0xFE0F, 0x9004, "lpm", ld_ptr, "Z"},
    {0xFE0F, 0x9005, "lpm", ld_ptr, "Z+"},
    {0xFE0F, 0x9006, "elpm", ld_ptr, "Z"},
    {0xFE0F, 0x9007, "elpm", ld_ptr, "Z+"},
    {0xFE0F, 0x9009, "ld", ld_ptr, "Y+"},
    {0xFE0F, 0x900A, "ld", ld_ptr, "-Y"},
    {0xFE0F, 0x900C, "ld", ld_ptr, "X"},
    {0xFE0F, 0x900D, "ld", ld_ptr, "X+"},
    {0xFE0F, 0x900E, "ld", ld_ptr, "-X"},
    {0xFE0F, 0x900F, "pop", rd},
    {0xFE0F, 0x9200, "sts", sts},
    {0xFE0F, 0x9201, "st", st_ptr, "Z+"},
    {0xFE0F, 0x9202, "st", st_ptr, "-Z"},
    {0xFE0F, 0x9204, "xch", st_ptr, "Z"},
    {0xFE0F, 0x9205, "las", st_ptr, "Z"},
    {0xFE0F, 0x9206, "lac", st_ptr, "Z"},
    {0xFE0F, 0x9207, "lat", st_ptr, "Z"},
    {0xFE0F, 0x9209, "st", st_ptr, "Y+"},
    {0xFE0F, 0x920A, "st", st_ptr, "-Y"},
    {0xFE0F, 0x920C, "st", st_ptr, "X"},
    {0xFE0F, 0x920D, "st", st_ptr, "X+"},
    {0xFE0F, 0x920E, "st", st_ptr, "-X"},
    {0xFE0F, 0x920F, "push", rd},

    {0xFE0F, 0x9400, "com", rd},
    {0xFE0F, 0x9401, "neg", rd},
    {0xFE0F, 0x9402, "swap", rd},
    {0xFE0F, 0x9403, "inc", rd},
    {0xFE0F, 0x9405, "asr", rd},
    {0xFE0F, 0x9406, "lsr", rd},
    {0xFE0F, 0x9407, "ror", rd},
    {0xFE0F, 0x940A, "dec", rd},
    {0xFFFF, 0x9409, "ijmp", none},
    {0xFFFF, 0x9419, "eijmp", none},
    {0xFFFF, 0x9508, "ret", none},
    {0xFFFF, 0x9509, "icall", none},
    {0xFFFF, 0x9518, "reti", none},
    {0xFFFF, 0x9519, "eicall", none},
    {0xFFFF, 0x9588, "sleep", none},
    {0xFFFF, 0x9598, "break", none},
    {0xFFFF, 0x95A8, "wdr", none},
    {0xFFFF, 0x95C8, "lpm", none},
    {0xFFFF, 0x95D8, "elpm", none},
    {0xFFFF, 0x95E8, "spm", none},
    {0xFFFF, 0x95F8, "spm", text_only, "spm Z+"},
    {0xFF0F, 0x940B, "des", des},
    {0xFE0E, 0x940C, "jmp", abs22},
    {0xFE0E, 0x940E, "call", abs22},
    {0xFF00, 0x9600, "adiw", word_k6},
    {0xFF00, 0x9700, "sbiw", word_k6},
    {0xFF00, 0x9800, "cbi", io_bit},
    {0xFF00, 0x9900, "sbic", io_bit},
    {0xFF00, 0x9A00, "sbi", io_bit},
    {0xFF00, 0x9B00, "sbis", io_bit},
    {0xFC00, 0x9C00, "mul", rd_rr},
    {0xF800, 0xB000, "in", in},
    {0xF800, 0xB800, "out", out},
    {0xF000, 0xC000, "rjmp", rel12},
    {0xF000, 0xD000, "rcall", rel12},
    {0xF000, 0xE000, "ldi", rd_k8},
    {0xFE08, 0xF800, "bld", rd_bit},
    {0xFE08, 0xFA00, "bst", rd_bit},
    {0xFE08, 0xFC00, "sbrc", rd_bit},
    {0xFE08, 0xFE00, "sbrs", rd_bit},
};

const char* const set_flags[] = {"sec", "sez", "sen", "sev", "ses", "seh", "set", "sei"};
const char* const clear_flags[] = {"clc", "clz", "cln", "clv", "cls", "clh", "clt", "cli"};
const char* const branch_set[] = {"brcs", "breq", "brmi", "brvs", "brlt", "brhs", "brts", "brie"};
const char* const branch_clear[] = {"brcc", "brne", "brpl", "brvc", "brge", "brhc", "brtc", "brid"};

}

int disassemble(uint32_t pc, uint16_t w, uint16_t next, std::string& text)
{
    char buf[64];
    int d5 = (w >> 4) & 0x1F, r5 = (w & 0x0F) | ((w >> 5) & 0x10);
    int k8 = (w & 0x0F) | ((w >> 4) & 0xF0);

    // flags and branches: a name per bit
    if ((w & 0xFF8F) == 0x9408 || (w & 0xFF8F) == 0x9488) {
        text = ((w & 0x0080) ? clear_flags : set_flags)[(w >> 4) & 7];
        return 2;
    }
    if ((w & 0xF800) == 0xF000) {
        int k = (w >> 3) & 0x7F;
        if (k & 0x40) k -= 0x80;
        std::snprintf(buf, sizeof(buf), "%s 0x%04X", ((w & 0x0400) ? branch_clear : branch_set)[w & 7], (unsigned)(pc + 2 + 2 * k));
        text = buf;
        return 2;
    }

    for (auto& o : ops) {
        if ((w & o.mask) != o.match) continue;
        int len = 2;
        switch (o.f) {
        case none: std::snprintf(buf, sizeof(buf), "%s", o.name); break;
        case text_only: std::snprintf(buf, sizeof(buf), "%s", o.ptr); break;
        case rd_rr: std::snprintf(buf, sizeof(buf), "%s r%d, r%d", o.name, d5, r5); break;
        case rd: std::snprintf(buf, sizeof(buf), "%s r%d", o.name, d5); break;
        case rd_k8: std::snprintf(buf, sizeof(buf), "%s r%d, 0x%02X", o.name, 16 + ((w >> 4) & 0xF), k8); break;
        case rd4_rr4: std::snprintf(buf, sizeof(buf), "%s r%d, r%d", o.name, 16 + ((w >> 4) & 0xF), 16 + (w & 0xF)); break;
        case rd3_rr3: std::snprintf(buf, sizeof(buf), "%s r%d, r%d", o.name, 16 + ((w >> 4) & 7), 16 + (w & 7)); break;
        case movw: std::snprintf(buf, sizeof(buf), "%s r%d, r%d", o.name, 2 * ((w >> 4) & 0xF), 2 * (w & 0xF)); break;
        case word_k6: std::snprintf(buf, sizeof(buf), "%s r%d, 0x%02X", o.name, 24 + 2 * ((w >> 4) & 3), (w & 0xF) | ((w >> 2) & 0x30)); break;
        case io_bit: std::snprintf(buf, sizeof(buf), "%s 0x%02X, %d", o.name, (w >> 3) & 0x1F, w & 7); break;
        case in: std::snprintf(buf, sizeof(buf), "%s r%d, 0x%02X", o.name, d5, (w & 0xF) | ((w >> 5) & 0x30)); break;
        case out: std::snprintf(buf, sizeof(buf), "%s 0x%02X, r%d", o.name, (w & 0xF) | ((w >> 5) & 0x30), d5); break;
        case rel12: {
            int k = w & 0xFFF;
            if (k & 0x800) k -= 0x1000;
            std::snprintf(buf, sizeof(buf), "%s 0x%04X", o.name, (unsigned)(pc + 2 + 2 * k));
            break;
        }
        case rel7: break;
        case rd_bit: std::snprintf(buf, sizeof(buf), "%s r%d, %d", o.name, d5, w & 7); break;
        case abs22: {
            uint32_t k = ((uint32_t)(((w >> 3) & 0x3E) | (w & 1)) << 16) | next;
            std::snprintf(buf, sizeof(buf), "%s 0x%04X", o.name, (unsigned)(2 * k));
            len = 4;
            break;
        }
        case lds:
            std::snprintf(buf, sizeof(buf), "%s r%d, 0x%04X", o.name, d5, next);
            len = 4;
            break;
        case sts:
            std::snprintf(buf, sizeof(buf), "%s 0x%04X, r%d", o.name, next, d5);
            len = 4;
            break;
        case ld_ptr: std::snprintf(buf, sizeof(buf), "%s r%d, %s", o.name, d5, o.ptr); break;
        case st_ptr: std::snprintf(buf, sizeof(buf), "%s %s, r%d", o.name, o.ptr, d5); break;
        case ldd:
        case std_: {
            int q = (w & 7) | ((w >> 7) & 0x18) | ((w >> 8) & 0x20);
            if (o.f == ldd) std::snprintf(buf, sizeof(buf), "%s r%d, %s+%d", o.name, d5, o.ptr, q);
            else std::snprintf(buf, sizeof(buf), "%s %s+%d, r%d", o.name, o.ptr, q, d5);
            break;
        }
        case des: std::snprintf(buf, sizeof(buf), "%s 0x%02X", o.name, (w >> 4) & 0xF); break;
        }
        text = buf;
        return len;
    }

    std::snprintf(buf, sizeof(buf), ".word 0x%04X", w);
    text = buf;
    return 2;
}

}
