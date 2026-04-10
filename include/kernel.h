#ifndef FLARE_KERNEL_H
#define FLARE_KERNEL_H

#include "boot.h"
#include "common.h"

typedef struct {
    u64 r15;
    u64 r14;
    u64 r13;
    u64 r12;
    u64 r11;
    u64 r10;
    u64 r9;
    u64 r8;
    u64 rsi;
    u64 rdi;
    u64 rbp;
    u64 rdx;
    u64 rcx;
    u64 rbx;
    u64 rax;
    u64 vector;
    u64 error_code;
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
} InterruptFrame;

extern BootInfo g_boot_info;
extern volatile u64 g_ticks;

enum {
    KEY_NONE = 0,
    KEY_ENTER = '\n',
    KEY_BACKSPACE = '\b',
    KEY_TAB = '\t',
    KEY_ESC = 0x100,
    KEY_F1,
    KEY_F2,
    KEY_F3,
    KEY_F4,
    KEY_F10,
};

enum {
    KEYBOARD_LAYOUT_QWERTY = 0,
    KEYBOARD_LAYOUT_AZERTY = 1,
};

void kernel_main(BootInfo *boot_info);
void panic(const char *message);

void memory_init(void);
void *kmalloc(usize size);
usize heap_bytes_used(void);
void *memcpy(void *dest, const void *src, usize count);
void *memset(void *dest, int value, usize count);
int memcmp(const void *lhs, const void *rhs, usize count);
usize strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, usize count);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, usize count);

void gdt_init(void);
void idt_init(void);
void paging_init(void);
u64 paging_mapped_bytes(void);
u64 paging_table_count(void);
void interrupt_dispatch(InterruptFrame *frame);

void pic_init(void);
void pic_send_eoi(u8 irq);
void pit_init(u32 hz);
void ps2_init(void);
void keyboard_handle_irq(void);
void keyboard_poll_event(void);
void keyboard_set_layout(int layout);
int keyboard_layout(void);
const char *keyboard_layout_name(void);

void ata_init(void);
int ata_present(void);
int ata_read_sector(u32 lba, void *buffer);
int ata_write_sector(u32 lba, const void *buffer);
int ata_flush(void);

void video_init(void);
int video_ready(void);
u32 video_width(void);
u32 video_height(void);
u32 video_bpp(void);
int video_font_height(void);
int video_text_scale(void);
int video_text_cell_width(void);
int video_text_cell_height(void);
void video_clear(u8 color);
void video_fill_rect(int x, int y, int w, int h, u8 color);
void video_draw_char(int x, int y, u8 fg, u8 bg, char ch);
void video_draw_char_scaled(int x, int y, int scale, u8 fg, u8 bg, char ch);
void video_draw_text(int x, int y, u8 fg, u8 bg, const char *text);

void console_init(void);
int console_ready(void);
int console_width(void);
int console_height(void);
void console_clear(u8 attr);
void console_fill_row(int row, u8 attr);
void console_write_at(int col, int row, u8 attr, const char *text);
void console_write_n_at(int col, int row, u8 attr, const char *text, usize n);
void console_set_cursor(int col, int row);

void fs_init(const void *base, u32 size);
int fs_is_writable(void);
const char *fs_backend_name(void);
int fs_write_file(const char *name, const char *data, usize size, int append);
int fs_remove_file(const char *name);
int fs_install(void);
usize fs_file_count(void);
const char *fs_file_name(usize index);
const char *fs_file_data(usize index, usize *size_out);

void shell_init(void);
void shell_render(void);
void shell_keyboard_event(u16 key, int pressed);
void shell_timer_tick(void);
int shell_needs_redraw(void);

void snake_start(u64 total_memory_bytes, u64 required_memory_bytes);
int snake_active(void);
void snake_keyboard_event(u16 key, int pressed);
void snake_timer_tick(void);
int snake_needs_redraw(void);
void snake_render(void);
int snake_consume_result(u32 *score_out);

void bsod_show(const char *title, const char *message, const InterruptFrame *frame);

void serial_init(void);
void serial_write(const char *text);

#endif
