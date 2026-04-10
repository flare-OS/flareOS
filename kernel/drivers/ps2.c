#include "kernel.h"
#include "x86_64.h"

static void ps2_wait_input(void) {
    for (u32 i = 0; i < 100000; ++i) {
        if ((inb(0x64) & 0x02) == 0) {
            return;
        }
    }
}

static void ps2_wait_output(void) {
    for (u32 i = 0; i < 100000; ++i) {
        if (inb(0x64) & 0x01) {
            return;
        }
    }
}

static void ps2_write_command(u8 value) {
    ps2_wait_input();
    outb(0x64, value);
}

static void ps2_write_data(u8 value) {
    ps2_wait_input();
    outb(0x60, value);
}

static u8 ps2_read_data(void) {
    ps2_wait_output();
    return inb(0x60);
}

void ps2_init(void) {
    ps2_write_command(0xad);
    ps2_write_command(0xa7);
    while (inb(0x64) & 0x01) {
        (void)inb(0x60);
    }

    ps2_write_command(0x20);
    u8 config = ps2_read_data();
    config |= 0x03;
    config &= ~0x20;
    ps2_write_command(0x60);
    ps2_write_data(config);

    ps2_write_command(0xae);

    ps2_write_data(0xf4);
    (void)ps2_read_data();
}
