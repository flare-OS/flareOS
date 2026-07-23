#include "kernel.h"
#include "x86_64.h"

BootInfo g_boot_info;

static u64 g_fault_recovery_rip = 0;
static u64 g_fault_recovery_rsp = 0;
static int g_fault_in_app = 0;

void set_app_fault_recovery(u64 rip, u64 rsp) {
    g_fault_in_app = 1;
    g_fault_recovery_rip = rip;
    g_fault_recovery_rsp = rsp;
}

void clear_app_fault_recovery(void) {
    g_fault_in_app = 0;
}

int is_in_app(void) {
    return g_fault_in_app;
}

void recover_from_app_fault(void) {
    serial_write("recovered\n");
}

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

static void append_hex_u64_serial(u64 value) {
    static const char digits[] = "0123456789ABCDEF";
    char buf[19];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; ++i)
        buf[2 + i] = digits[(value >> (60 - i * 4)) & 0x0f];
    buf[18] = '\0';
    serial_write(buf);
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

static void dump_regs(const char *label, u64 val) {
    char buf[32]; usize len = 0; buf[0] = '\0';
    append_text(buf, sizeof(buf), &len, label);
    append_text(buf, sizeof(buf), &len, "=");
    append_hex_u64(buf, sizeof(buf), &len, val);
    serial_write(buf);
    serial_write(" ");
}

static void panic_exception(InterruptFrame *frame) {
    if (g_fault_in_app && g_fault_recovery_rip) {
        serial_write("\n=== APP FAULT ===\n");
        serial_write(exception_name(frame->vector));
        serial_write(" in app, recovering\n");
        shell_cleanup_apps();
        frame->rip = g_fault_recovery_rip;
        frame->rsp = g_fault_recovery_rsp;
        frame->cs = 0x08;
        frame->ss = 0x10;
        frame->rflags &= ~0x200u;
        g_fault_in_app = 0;
        return;
    }

    serial_write("\n=== EXCEPTION ===\n");
    serial_write(exception_name(frame->vector));
    serial_write("\n");

    dump_regs("VEC", frame->vector);
    dump_regs("ERR", frame->error_code);
    dump_regs("RIP", frame->rip);
    dump_regs("CS", frame->cs);
    dump_regs("RFLAGS", frame->rflags);
    dump_regs("RSP", frame->rsp);
    dump_regs("SS", frame->ss);
    serial_write("\n");

    if (frame->error_code & 1) serial_write(" P=prot ");
    else serial_write(" P=not ");
    if (frame->error_code & 2) serial_write("W=write ");
    else serial_write("W=read ");
    if (frame->error_code & 4) serial_write("U=user");
    else serial_write("U=supervisor");
    serial_write("\n");

    if (frame->vector == 14) {
        char cr2str[32]; usize cl = 0; cr2str[0] = '\0';
        append_text(cr2str, sizeof(cr2str), &cl, "CR2=");
        append_hex_u64(cr2str, sizeof(cr2str), &cl, read_cr2());
        serial_write(cr2str);
        serial_write("\n");
    }

    serial_write("RAX="); append_hex_u64_serial(frame->rax);
    serial_write(" RBX="); append_hex_u64_serial(frame->rbx);
    serial_write(" RCX="); append_hex_u64_serial(frame->rcx);
    serial_write(" RDX="); append_hex_u64_serial(frame->rdx);
    serial_write("\n");
    serial_write("RDI="); append_hex_u64_serial(frame->rdi);
    serial_write(" RSI="); append_hex_u64_serial(frame->rsi);
    serial_write(" RBP="); append_hex_u64_serial(frame->rbp);
    serial_write(" R8="); append_hex_u64_serial(frame->r8);
    serial_write(" R9="); append_hex_u64_serial(frame->r9);
    serial_write("\n");
    serial_write("R10="); append_hex_u64_serial(frame->r10);
    serial_write(" R11="); append_hex_u64_serial(frame->r11);
    serial_write(" R12="); append_hex_u64_serial(frame->r12);
    serial_write(" R13="); append_hex_u64_serial(frame->r13);
    serial_write(" R14="); append_hex_u64_serial(frame->r14);
    serial_write(" R15="); append_hex_u64_serial(frame->r15);
    serial_write("\n");

    bsod_show(exception_name(frame->vector), "see serial", frame);
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
    net_init();
    shell_init();
    outb(0xe9, 'G');
    shell_render();

    sti();

    for (;;) {
        u64 rsp;
        __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
        set_app_fault_recovery((u64)&&recovery_point, rsp);
        keyboard_poll_event();
        browser_tick();
        if (shell_needs_redraw())
            shell_render();
        clear_app_fault_recovery();
        recovery_point:;
        hlt();
    }
}
