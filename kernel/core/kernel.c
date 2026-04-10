#include "kernel.h"
#include "x86_64.h"

BootInfo g_boot_info;

static void panic_halt(void) {
    cli();
    for (;;) {
        hlt();
    }
}

static void append_char(char *buffer, usize size, usize *len, char ch) {
    if (*len + 1 >= size) {
        return;
    }
    buffer[*len] = ch;
    ++(*len);
    buffer[*len] = '\0';
}

static void append_text(char *buffer, usize size, usize *len, const char *text) {
    while (*text) {
        append_char(buffer, size, len, *text++);
    }
}

static void append_hex_u64(char *buffer, usize size, usize *len, u64 value) {
    static const char digits[] = "0123456789ABCDEF";

    append_text(buffer, size, len, "0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        append_char(buffer, size, len, digits[(value >> shift) & 0x0f]);
    }
}

static const char *exception_name(u64 vector) {
    static const char *names[32] = {
        "Divide Error", "Debug", "NMI", "Breakpoint",
        "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
        "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
        "Stack Fault", "General Protection Fault", "Page Fault", "Reserved",
        "x87 Floating Point", "Alignment Check", "Machine Check", "SIMD Floating Point",
        "Virtualization", "Control Protection", "Reserved", "Reserved",
        "Reserved", "Reserved", "Reserved", "Reserved",
        "Hypervisor Injection", "VMM Communication", "Security", "Reserved",
    };

    if (vector < ARRAY_SIZE(names) && names[vector]) {
        return names[vector];
    }
    return "CPU Exception";
}

static void panic_exception(InterruptFrame *frame) {
    char detail[160];
    usize len = 0;

    detail[0] = '\0';
    append_text(detail, sizeof(detail), &len, "vector=");
    append_hex_u64(detail, sizeof(detail), &len, frame->vector);
    append_text(detail, sizeof(detail), &len, " error=");
    append_hex_u64(detail, sizeof(detail), &len, frame->error_code);
    if (frame->vector == 14) {
        append_text(detail, sizeof(detail), &len, " cr2=");
        append_hex_u64(detail, sizeof(detail), &len, read_cr2());
    }

    serial_write("panic: ");
    serial_write(exception_name(frame->vector));
    serial_write(" ");
    serial_write(detail);
    serial_write("\n");
    bsod_show(exception_name(frame->vector), detail, frame);
    panic_halt();
}

static void kernel_apply_saved_layout(void) {
    for (usize i = 0; i < fs_file_count(); ++i) {
        usize size = 0;
        const char *name = fs_file_name(i);
        const char *data;

        if (!name || strcmp(name, "keyboard.cfg") != 0) {
            continue;
        }

        data = fs_file_data(i, &size);
        if (!data) {
            break;
        }
        if (size >= 6 && strncmp(data, "azerty", 6) == 0) {
            keyboard_set_layout(KEYBOARD_LAYOUT_AZERTY);
            serial_write("kbd: loaded azerty\n");
            break;
        }
        if (size >= 6 && strncmp(data, "qwerty", 6) == 0) {
            keyboard_set_layout(KEYBOARD_LAYOUT_QWERTY);
            serial_write("kbd: loaded qwerty\n");
            break;
        }
    }
}

void panic(const char *message) {
    serial_write("panic: ");
    serial_write(message);
    serial_write("\n");
    bsod_show("Kernel Panic", message, NULL);
    panic_halt();
}

void interrupt_dispatch(InterruptFrame *frame) {
    if (frame->vector < 32) {
        panic_exception(frame);
    }

    switch (frame->vector) {
    case 32:
        ++g_ticks;
        shell_timer_tick();
        pic_send_eoi(0);
        break;
    case 33:
        keyboard_handle_irq();
        pic_send_eoi(1);
        break;
    default:
        pic_send_eoi((u8)(frame->vector - 32));
        break;
    }
}

void kernel_main(BootInfo *boot_info) {
    outb(0xe9, 'K');
    outb(0xe9, 'M');
    outb(0xe9, (u8)((usize)boot_info & 0xffu));
    serial_init();
    serial_write("flareOS kernel start\n");

    if (!boot_info || boot_info->magic != BOOTINFO_MAGIC) {
        panic("boot info missing");
    }
    memcpy(&g_boot_info, boot_info, sizeof(g_boot_info));
    console_init();

    memory_init();
    gdt_init();
    paging_init();
    idt_init();
    pic_init();
    pit_init(g_boot_info.timer_hz ? g_boot_info.timer_hz : 60);
    outb(0xe9, 'T');
    ps2_init();
    keyboard_set_layout(KEYBOARD_LAYOUT_QWERTY);
    fs_init((const void *)(usize)g_boot_info.fs_addr, g_boot_info.fs_size);
    kernel_apply_saved_layout();
    shell_init();
    outb(0xe9, 'G');
    shell_render();

    sti();

    for (;;) {
        keyboard_poll_event();
        if (shell_needs_redraw()) {
            shell_render();
        }
        hlt();
    }
}
