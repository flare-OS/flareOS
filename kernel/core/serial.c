#include "kernel.h"
#include "x86_64.h"

void serial_init(void) {
    outb(0x3f8 + 1, 0x00);
    outb(0x3f8 + 3, 0x80);
    outb(0x3f8 + 0, 0x03);
    outb(0x3f8 + 1, 0x00);
    outb(0x3f8 + 3, 0x03);
    outb(0x3f8 + 2, 0xc7);
    outb(0x3f8 + 4, 0x0b);
}

static void serial_putc(char ch) {
    while ((inb(0x3f8 + 5) & 0x20) == 0) {
    }
    outb(0x3f8, (u8)ch);
}

void serial_write(const char *text) {
    while (*text) {
        if (*text == '\n') {
            serial_putc('\r');
        }
        serial_putc(*text++);
    }
}
