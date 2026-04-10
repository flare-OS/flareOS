#include "kernel.h"
#include "x86_64.h"

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

static void write_register_line(int row, const char *name, u64 value) {
    char line[48];
    usize len = 0;

    line[0] = '\0';
    append_text(line, sizeof(line), &len, name);
    append_text(line, sizeof(line), &len, " ");
    append_hex_u64(line, sizeof(line), &len, value);
    console_write_at(2, row, 0x1f, line);
}

void bsod_show(const char *title, const char *message, const InterruptFrame *frame) {
    int row = 1;

    if (!console_ready()) {
        return;
    }

    cli();
    console_clear(0x1f);
    console_write_at(2, row++, 0x1f, "A problem has been detected and flareOS has been shut down.");
    ++row;
    console_write_at(2, row++, 0x1f, title ? title : "System Failure");
    console_write_at(2, row++, 0x1f, message ? message : "No additional information.");
    ++row;

    if (frame) {
        write_register_line(row++, "RIP", frame->rip);
        write_register_line(row++, "RSP", frame->rsp);
        write_register_line(row++, "RFLAGS", frame->rflags);
        write_register_line(row++, "VECTOR", frame->vector);
        write_register_line(row++, "ERROR", frame->error_code);
        if (frame->vector == 14) {
            write_register_line(row++, "CR2", read_cr2());
        }
    }

    row = console_height() - 2;
    console_write_at(2, row, 0x1f, "Restart the virtual machine to continue.");
    console_set_cursor(0, console_height() - 1);
}
