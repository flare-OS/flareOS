#include "kernel.h"
#include "x86_64.h"

struct __attribute__((packed)) IdtEntry {
    u16 offset_low;
    u16 selector;
    u8 ist;
    u8 type_attr;
    u16 offset_mid;
    u32 offset_high;
    u32 zero;
};

struct __attribute__((packed)) IdtPointer {
    u16 limit;
    u64 base;
};

extern void *isr_stub_table[];
static struct IdtEntry idt[256];

static void idt_set_gate(int vector, void *handler) {
    u64 addr = (u64)handler;
    idt[vector].offset_low = (u16)(addr & 0xffff);
    idt[vector].selector = 0x08;
    idt[vector].ist = 0;
    idt[vector].type_attr = 0x8e;
    idt[vector].offset_mid = (u16)((addr >> 16) & 0xffff);
    idt[vector].offset_high = (u32)(addr >> 32);
    idt[vector].zero = 0;
}

void idt_init(void) {
    memset(idt, 0, sizeof(idt));
    for (int i = 0; i < 48; ++i) {
        idt_set_gate(i, isr_stub_table[i]);
    }
    struct IdtPointer ptr = {
        .limit = sizeof(idt) - 1,
        .base = (u64)&idt[0],
    };
    lidt(&ptr);
}
