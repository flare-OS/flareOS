#include "kernel.h"
#include "x86_64.h"

void pic_init(void) {
    outb(0x20, 0x11);
    io_wait();
    outb(0xa0, 0x11);
    io_wait();
    outb(0x21, 0x20);
    io_wait();
    outb(0xa1, 0x28);
    io_wait();
    outb(0x21, 0x04);
    io_wait();
    outb(0xa1, 0x02);
    io_wait();
    outb(0x21, 0x01);
    io_wait();
    outb(0xa1, 0x01);
    io_wait();
    outb(0x21, 0x00);
    outb(0xa1, 0x00);
}

void pic_send_eoi(u8 irq) {
    if (irq >= 8) {
        outb(0xa0, 0x20);
    }
    outb(0x20, 0x20);
}
