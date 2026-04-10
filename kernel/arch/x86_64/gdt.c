#include "kernel.h"
#include "x86_64.h"

struct __attribute__((packed)) GdtPointer {
    u16 limit;
    u64 base;
};

static u64 gdt[3] = {
    0x0000000000000000ull,
    0x00af9a000000ffffull,
    0x00af92000000ffffull,
};

void gdt_init(void) {
    struct GdtPointer ptr = {
        .limit = sizeof(gdt) - 1,
        .base = (u64)&gdt[0],
    };
    lgdt(&ptr);
    __asm__ volatile(
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        "pushq $0x08\n"
        "lea 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        :
        :
        : "rax", "memory");
}
