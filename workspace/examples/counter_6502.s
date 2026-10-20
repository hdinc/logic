; program of counter_6502.bin: a 64 KB ram image, the code at f000 and the
; reset vector at fffc. every write goes into the register on the schematic,
; so the hex display shows x.

        .org $f000
start:  ldx #0
loop:   inx
        stx $8000       ; the only write
        ldy #0
wait:   dey             ; 256 * 5 cycles
        bne wait
        jmp loop

        .org $fffc
        .word start     ; reset
