# the entry of the test firmware: a stack at the end of the ram, then main
    .section .text.init
    .globl _start
_start:
    la sp, _stack_top
    call main
1:  j 1b
