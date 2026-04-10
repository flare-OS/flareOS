#include "kernel.h"

static int g_console_is_ready = 0;

static u8 attr_fg(u8 attr) {
    return attr & 0x0f;
}

static u8 attr_bg(u8 attr) {
    return (attr >> 4) & 0x0f;
}

int console_ready(void) {
    return g_console_is_ready;
}

int console_width(void) {
    return video_ready() ? (int)(video_width() / (u32)video_text_cell_width()) : 0;
}

int console_height(void) {
    return video_ready() ? (int)(video_height() / (u32)video_text_cell_height()) : 0;
}

void console_init(void) {
    video_init();
    g_console_is_ready = video_ready();
    if (g_console_is_ready) {
        console_clear(0x07);
    }
}

void console_clear(u8 attr) {
    if (!g_console_is_ready) {
        return;
    }
    video_clear(attr_bg(attr));
}

void console_fill_row(int row, u8 attr) {
    if (!g_console_is_ready || row < 0 || row >= console_height()) {
        return;
    }
    video_fill_rect(0, row * video_text_cell_height(), (int)video_width(), video_text_cell_height(), attr_bg(attr));
}

void console_write_n_at(int col, int row, u8 attr, const char *text, usize n) {
    int x_inset = video_text_scale() / 2;

    if (!g_console_is_ready || !text || row < 0 || row >= console_height() || col >= console_width()) {
        return;
    }
    if (col < 0) {
        int drop = -col;
        if ((usize)drop >= n) {
            return;
        }
        n -= (usize)drop;
        text += drop;
        col = 0;
    }

    for (usize idx = 0; idx < n && text[idx] && col + (int)idx < console_width(); ++idx) {
        char ch = text[idx];
        if (ch < ' ' || ch > '~') {
            ch = '.';
        }
        video_draw_char_scaled((col + (int)idx) * video_text_cell_width() + x_inset,
                               row * video_text_cell_height() + video_text_scale(),
                               video_text_scale(),
                               attr_fg(attr),
                               attr_bg(attr),
                               ch);
    }
}

void console_write_at(int col, int row, u8 attr, const char *text) {
    console_write_n_at(col, row, attr, text, strlen(text));
}

void console_set_cursor(int col, int row) {
    int x_inset = video_text_scale() / 2;

    if (!g_console_is_ready || row < 0 || row >= console_height() || col < 0 || col >= console_width()) {
        return;
    }
    video_fill_rect(col * video_text_cell_width() + x_inset,
                    row * video_text_cell_height() + video_text_scale() + video_font_height() * video_text_scale() - 1,
                    8 * video_text_scale(),
                    video_text_scale(),
                    15);
}
