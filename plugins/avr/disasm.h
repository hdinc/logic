#pragma once

#include <cstdint>
#include <string>

namespace avr {

// one instruction at byte address pc, from its first word w and the word
// after it (used by 32 bit instructions). returns the length in bytes, 2 or
// 4. jump targets are byte addresses like avr-objdump's comments.
int disassemble(uint32_t pc, uint16_t w, uint16_t next, std::string& text);

}
