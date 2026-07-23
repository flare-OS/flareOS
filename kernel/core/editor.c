#include "kernel.h"

#define EDITOR_MAX_LINES 500
#define EDITOR_MAX_LINE 256

static int g_active, g_dirty;
static char g_lines[EDITOR_MAX_LINES][EDITOR_MAX_LINE];
static int g_line_count;
static int g_cx, g_cy, g_scroll;
static int g_modified;
static char g_filename[64];
static int g_quit;

static int editor_avail_cols(void) {
    int c = (int)video_width() / 8 - 6;
    return c < 10 ? 10 : c;
}
static int editor_avail_rows(void) {
    int r = ((int)video_height() - 32) / video_font_height();
    return r < 5 ? 5 : r;
}

static void append_char(char *b, usize sz, usize *len, char ch) {
    if (*len + 1 >= sz) return; b[*len] = ch; ++(*len); b[*len] = '\0';
}
static void append_text(char *b, usize sz, usize *len, const char *t) {
    while (*t) append_char(b, sz, len, *t++);
}
static void append_u32_dec(char *b, usize sz, usize *len, u32 v) {
    char d[16]; int c = 0;
    do { d[c++] = (char)('0' + (v % 10u)); v /= 10u; } while (v && c < 16);
    while (c > 0) append_char(b, sz, len, d[--c]);
}

void editor_start(const char *filename) {
    strncpy(g_filename, filename, sizeof(g_filename) - 1);
    g_filename[sizeof(g_filename) - 1] = '\0';
    g_line_count = 1;
    g_lines[0][0] = '\0';
    g_cx = 0; g_cy = 0; g_scroll = 0;
    g_modified = 0; g_quit = 0;
    g_active = 1; g_dirty = 1;

    for (usize i = 0; i < fs_file_count(); ++i) {
        if (strcmp(fs_file_name(i), g_filename) == 0) {
            usize sz = 0;
            const char *data = fs_file_data(i, &sz);
            g_line_count = 0;
            usize pos = 0;
            while (pos < sz && g_line_count < EDITOR_MAX_LINES) {
                int col = 0;
                while (pos < sz && data[pos] != '\n' && col < EDITOR_MAX_LINE - 1) {
                    char ch = data[pos];
                    if (ch >= ' ') g_lines[g_line_count][col++] = ch;
                    ++pos;
                }
                g_lines[g_line_count][col] = '\0';
                ++g_line_count;
                if (pos < sz && data[pos] == '\n') ++pos;
            }
            if (g_line_count == 0) { g_line_count = 1; g_lines[0][0] = '\0'; }
            break;
        }
    }
}

int editor_active(void) { return g_active && !g_quit; }

int editor_needs_redraw(void) { return g_dirty; }

static void editor_insert_char(char ch) {
    char *line = g_lines[g_cy];
    int len = (int)strlen(line);
    if (len >= EDITOR_MAX_LINE - 1) return;
    for (int i = len; i >= g_cx; --i) line[i + 1] = line[i];
    line[g_cx] = ch;
    ++g_cx;
    g_modified = 1;
    g_dirty = 1;
}

static void editor_newline(void) {
    if (g_line_count >= EDITOR_MAX_LINES) return;
    char *line = g_lines[g_cy];
    int rest = (int)strlen(line) - g_cx;
    if (rest < 0) rest = 0;
    for (int i = g_line_count; i > g_cy + 1; --i)
        strcpy(g_lines[i], g_lines[i - 1]);
    memcpy(g_lines[g_cy + 1], line + g_cx, (usize)rest + 1);
    line[g_cx] = '\0';
    ++g_line_count;
    g_cy++; g_cx = 0;
    g_modified = 1;
    g_dirty = 1;
}

static void editor_backspace(void) {
    if (g_cx > 0) {
        char *line = g_lines[g_cy];
        for (int i = g_cx - 1; line[i]; ++i) line[i] = line[i + 1];
        --g_cx;
        g_modified = 1; g_dirty = 1;
    } else if (g_cy > 0) {
        char *prev = g_lines[g_cy - 1];
        int plen = (int)strlen(prev);
        if (plen + (int)strlen(g_lines[g_cy]) < EDITOR_MAX_LINE - 1) {
            strcpy(prev + plen, g_lines[g_cy]);
            for (int i = g_cy; i < g_line_count - 1; ++i)
                strcpy(g_lines[i], g_lines[i + 1]);
            --g_line_count;
            g_cy--; g_cx = plen;
            g_modified = 1; g_dirty = 1;
        }
    }
}

static void editor_delete(void) {
    char *line = g_lines[g_cy];
    if (line[g_cx]) {
        for (int i = g_cx; line[i]; ++i) line[i] = line[i + 1];
        g_modified = 1; g_dirty = 1;
    } else if (g_cy + 1 < g_line_count) {
        int len = (int)strlen(line);
        char *next = g_lines[g_cy + 1];
        if (len + (int)strlen(next) < EDITOR_MAX_LINE - 1) {
            strcpy(line + len, next);
            for (int i = g_cy + 1; i < g_line_count - 1; ++i)
                strcpy(g_lines[i], g_lines[i + 1]);
            --g_line_count;
            g_modified = 1; g_dirty = 1;
        }
    }
}

static void editor_save(void) {
    if (!fs_is_writable()) return;
    static char buf[16384];
    int pos = 0;
    for (int i = 0; i < g_line_count; ++i) {
        int len = (int)strlen(g_lines[i]);
        if (pos + len + 1 > (int)sizeof(buf)) break;
        if (len > 0) { memcpy(buf + pos, g_lines[i], (usize)len); pos += len; }
        buf[pos++] = '\n';
    }
    if (pos > 0) fs_write_file(g_filename, buf, (usize)pos, 0);
    g_modified = 0;
    g_dirty = 1;
}

void editor_keyboard_event(u16 key, int pressed) {
    if (!g_active || !pressed || g_quit) return;

    if (key == (0x200u | 'q')) { g_quit = 1; g_active = 0; return; }
    if (key == (0x200u | 's')) { editor_save(); return; }

    if (key == KEY_ESC) { g_quit = 1; g_active = 0; return; }
    if (key == KEY_UP && g_cy > 0) { --g_cy; g_dirty = 1; }
    else if (key == KEY_DOWN && g_cy + 1 < g_line_count) { ++g_cy; g_dirty = 1; }
    else if (key == KEY_LEFT && g_cx > 0) { --g_cx; g_dirty = 1; }
    else if (key == KEY_RIGHT) {
        int len = (int)strlen(g_lines[g_cy]);
        if (g_cx < len) { ++g_cx; g_dirty = 1; }
    } else if (key == KEY_HOME) { g_cx = 0; g_dirty = 1; }
    else if (key == KEY_END) { g_cx = (int)strlen(g_lines[g_cy]); g_dirty = 1; }
    else if (key == KEY_PGUP) { g_cy -= editor_avail_rows(); if (g_cy < 0) g_cy = 0; g_dirty = 1; }
    else if (key == KEY_PGDN) {
        g_cy += editor_avail_rows();
        if (g_cy >= g_line_count) g_cy = g_line_count - 1;
        g_dirty = 1;
    } else if (key == KEY_BACKSPACE) { editor_backspace(); }
    else if (key == KEY_DEL) { editor_delete(); }
    else if (key == KEY_ENTER) { editor_newline(); }
    else if (key >= ' ' && key <= '~') { editor_insert_char((char)key); }
    else if (key == KEY_TAB) { editor_insert_char(' '); editor_insert_char(' '); }

    if (g_cy < g_scroll) g_scroll = g_cy;
    if (g_cy >= g_scroll + editor_avail_rows()) g_scroll = g_cy - editor_avail_rows() + 1;
    int max_cx = (int)strlen(g_lines[g_cy]);
    int cols = editor_avail_cols();
    if (g_cx > max_cx) g_cx = max_cx;
    if (g_cx - 2 < 0) g_cx = 2;
    if (g_cx >= cols) g_cx = cols - 1;
}

void editor_render(void) {
    if (!g_active) return;
    int fh = video_font_height();
    int cols = editor_avail_cols();
    int rows = editor_avail_rows();

    video_clear(0);
    video_fill_rect(0, 0, (int)video_width(), 16, 1);
    video_fill_rect(0, (int)video_height() - 16, (int)video_width(), 16, 1);

    {
        char hdr[128]; usize hl = 0;
        hdr[0] = '\0';
        append_text(hdr, sizeof(hdr), &hl, "EDT ");
        append_text(hdr, sizeof(hdr), &hl, g_filename);
        if (g_modified) append_text(hdr, sizeof(hdr), &hl, " *");
        video_draw_text(4, 4, 15, 1, hdr);
    }

    for (int r = 0; r < rows && g_scroll + r < g_line_count; ++r) {
        int line_idx = g_scroll + r;
        int y = 16 + r * fh;
        u8 line_bg = (line_idx == g_cy) ? 7 : 0;
        u8 line_fg = (line_idx == g_cy) ? 0 : 15;
        char num[8]; usize nl = 0;
        num[0] = '\0';
        append_u32_dec(num, sizeof(num), &nl, (u32)(line_idx + 1));
        while (nl < 4) { num[nl++] = ' '; num[nl] = '\0'; }
        num[nl] = ':'; num[nl + 1] = '\0';
        video_draw_text(0, y, 8, line_bg, num);

        char text[EDITOR_MAX_LINE];
        int ti = 0;
        for (int i = 0; g_lines[line_idx][i] && ti < cols; ++i)
            text[ti++] = g_lines[line_idx][i];
        text[ti] = '\0';
        int tx = 6 * 8;
        video_fill_rect(tx, y, (int)video_width() - tx, fh, line_bg);
        video_draw_text(tx, y, line_fg, line_bg, text);
    }

    {
        char st[128]; usize sl = 0;
        st[0] = '\0';
        append_text(st, sizeof(st), &sl, "Ctrl+S save  Ctrl+Q quit  line ");
        append_u32_dec(st, sizeof(st), &sl, (u32)(g_cy + 1));
        append_text(st, sizeof(st), &sl, "/");
        append_u32_dec(st, sizeof(st), &sl, (u32)g_line_count);
        append_text(st, sizeof(st), &sl, " col ");
        append_u32_dec(st, sizeof(st), &sl, (u32)(g_cx + 1));
        video_draw_text(4, (int)video_height() - 12, 15, 1, st);
    }

    int cx = (6 + g_cx) * 8;
    int cy = 16 + (g_cy - g_scroll) * fh;
    if (g_cy >= g_scroll && g_cy < g_scroll + rows)
        video_fill_rect(cx, cy, 8, fh, 15);

    g_dirty = 0;
}
