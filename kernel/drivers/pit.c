#include "kernel.h"
#include "x86_64.h"

volatile u64 g_ticks = 0;

void pit_init(u32 hz) {
    u32 divisor = 1193182u / hz;
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xff);
    outb(0x40, (divisor >> 8) & 0xff);
}
