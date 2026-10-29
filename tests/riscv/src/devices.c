// tests the clint, plic and uart parts with the cpu, on the memory map of
// the linux example: uart 0x01000000 (plic source 1), clint 0x02000000,
// plic 0x03000000. prints what it checks on the uart, echoes typed
// characters in upper case and writes tohost (1 is a pass) after a newline.

#include <stdint.h>

#define REG8(a) (*(volatile uint8_t*)(a))
#define REG32(a) (*(volatile uint32_t*)(a))

#define UART 0x01000000u
#define CLINT 0x02000000u
#define PLIC 0x03000000u

#define MSIP REG32(CLINT)
#define MTIMECMP_LO REG32(CLINT + 0x4000)
#define MTIMECMP_HI REG32(CLINT + 0x4004)
#define MTIME_LO REG32(CLINT + 0xBFF8)
#define MTIME_HI REG32(CLINT + 0xBFFC)

#define PRIORITY(n) REG32(PLIC + 4 * (n))
#define ENABLE_M REG32(PLIC + 0x2000)
#define THRESHOLD_M REG32(PLIC + 0x200000)
#define CLAIM_M REG32(PLIC + 0x200004)

#define read_csr(r) ({ uint32_t v; __asm__ volatile("csrr %0, " #r : "=r"(v)); v; })
#define write_csr(r, v) __asm__ volatile("csrw " #r ", %0" ::"r"(v))
#define set_csr(r, v) __asm__ volatile("csrs " #r ", %0" ::"r"(v))

volatile uint32_t tohost __attribute__((section(".tohost")));

static volatile int timer_hits, soft_hits, done;

static void putc(char c)
{
    while (!(REG8(UART + 5) & 0x20))
        ;
    REG8(UART) = c;
}

static void puts(const char* s)
{
    while (*s)
        putc(*s++);
}

static uint64_t mtime(void)
{
    uint32_t hi, lo;
    do {
        hi = MTIME_HI;
        lo = MTIME_LO;
    } while (hi != MTIME_HI);
    return (uint64_t)hi << 32 | lo;
}

static void fail(const char* why)
{
    puts(why);
    tohost = 3;
    for (;;)
        ;
}

__attribute__((interrupt("machine"))) static void trap(void)
{
    uint32_t cause = read_csr(mcause);
    if (cause == 0x80000007u) {
        timer_hits++;
        MTIMECMP_HI = 0xFFFFFFFFu;
    } else if (cause == 0x80000003u) {
        soft_hits++;
        MSIP = 0;
    } else if (cause == 0x8000000Bu) {
        uint32_t id = CLAIM_M;
        if (id != 1) fail("wrong source\n");
        // everything in the fifo
        while (REG8(UART + 5) & 1) {
            char c = REG8(UART);
            if (c == '\n') done = 1;
            putc(c >= 'a' && c <= 'z' ? c - 32 : c);
        }
        CLAIM_M = id;
    } else {
        fail("unexpected trap\n");
    }
}

int main(void)
{
    puts("hello\n");
    write_csr(mtvec, (uint32_t)trap);

    // a timer interrupt 100 us from now
    uint64_t at = mtime() + 1000;
    MTIMECMP_HI = 0xFFFFFFFFu;
    MTIMECMP_LO = (uint32_t)at;
    MTIMECMP_HI = (uint32_t)(at >> 32);
    write_csr(mie, 1u << 7);
    set_csr(mstatus, 1u << 3);
    while (!timer_hits)
        __asm__ volatile("wfi");
    if (mtime() < at) fail("timer early\n");
    puts("timer\n");

    // a software interrupt
    set_csr(mie, 1u << 3);
    MSIP = 1;
    while (!soft_hits)
        __asm__ volatile("wfi");
    puts("soft\n");

    // received characters through the plic, the fifo's trigger level is 14
    // so single characters come with the timeout
    REG8(UART + 3) = 0x80;
    REG8(UART + 0) = 5;
    REG8(UART + 1) = 0;
    REG8(UART + 3) = 0x03;
    REG8(UART + 2) = 0xC7;
    REG8(UART + 1) = 1;
    PRIORITY(1) = 1;
    ENABLE_M = 1u << 1;
    THRESHOLD_M = 0;
    set_csr(mie, 1u << 11);
    puts("ready\n");
    while (!done)
        __asm__ volatile("wfi");
    puts("bye\n");
    tohost = 1;
    for (;;)
        __asm__ volatile("wfi");
}
