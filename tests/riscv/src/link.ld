/* bare-metal firmware for the tests: 64 KiB of ram at 0x80000000 */
OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { ram (rwx) : ORIGIN = 0x80000000, LENGTH = 64K }
SECTIONS
{
    .text : { *(.text.init) *(.text .text.*) } > ram
    .rodata : { *(.rodata .rodata.* .srodata .srodata.*) } > ram
    .data : { *(.data .data.* .sdata .sdata.*) } > ram
    .tohost : { *(.tohost) } > ram
    .bss : { *(.bss .bss.* .sbss .sbss.* COMMON) } > ram
    _stack_top = ORIGIN(ram) + LENGTH(ram);
}
