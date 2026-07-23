#include "kernel.h"

#define BROWSER_MAX_BYTES 8192
#define BROWSER_MAX_LINES 500
#define BROWSER_MAX_LINE 128
#define BROWSER_MAX_LINKS 200

static int g_active, g_dirty;
static char g_lines[BROWSER_MAX_LINES][BROWSER_MAX_LINE];
static int g_line_count;
static int g_scroll;
static char g_url[256];

typedef struct { char url[192]; int line; } BLink;
static BLink g_links[BROWSER_MAX_LINKS];
static int g_link_count;
static int g_sel_link;

static u8 g_line_fg[BROWSER_MAX_LINES];
static u8 g_line_bg[BROWSER_MAX_LINES];
static int g_line_border[BROWSER_MAX_LINES];
static int g_line_shadow[BROWSER_MAX_LINES];

static int brows_cols(void) {
    int c = (int)video_width() / 8;
    return c < 20 ? 20 : c;
}
static int brows_rows(void) {
    int r = ((int)video_height() - 32) / video_font_height();
    return r < 3 ? 3 : r;
}

static void app_char(char *b, usize sz, usize *len, char ch) {
    if (*len + 1 >= sz) return; b[*len] = ch; ++(*len); b[*len] = '\0';
}
static void app_text(char *b, usize sz, usize *len, const char *t) {
    while (*t) app_char(b, sz, len, *t++);
}

static const u32 vga_rgb[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

static int color_name_to_vga(const char *s, int slen) {
    static const char *names[16] = {
        "black", "blue", "green", "cyan",
        "red", "magenta", "brown", "lightgray",
        "darkgray", "lightblue", "lightgreen", "lightcyan",
        "lightred", "lightmagenta", "yellow", "white"
    };
    for (int i = 0; i < 16; ++i) {
        int nl = (int)strlen(names[i]);
        if (slen == nl && strncmp(s, names[i], (usize)nl) == 0)
            return i;
    }
    if (slen == 4 && strncmp(s, "gray", 4) == 0) return 8;
    if (slen == 5 && strncmp(s, "grey", 4) == 0) return 8;
    if (slen == 6 && strncmp(s, "silver", 6) == 0) return 7;
    if (slen == 4 && strncmp(s, "aqua", 4) == 0) return 3;
    if (slen == 5 && strncmp(s, "navy", 4) == 0) return 1;
    if (slen == 6 && strncmp(s, "maroon", 6) == 0) return 4;
    if (slen == 4 && strncmp(s, "teal", 4) == 0) return 3;
    if (slen == 6 && strncmp(s, "purple", 6) == 0) return 5;
    if (slen == 5 && strncmp(s, "olive", 5) == 0) return 6;
    if (slen == 4 && strncmp(s, "lime", 4) == 0) return 10;
    if (slen == 7 && strncmp(s, "fuchsia", 7) == 0) return 13;
    if (slen == 6 && strncmp(s, "orange", 6) == 0) return 6;
    return -1;
}

static int hex_char_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static int hex_color_to_vga(const char *s, int slen) {
    int r = 0, g = 0, b = 0;
    if (slen == 4 && s[0] == '#') {
        r = hex_char_val(s[1]) * 17;
        g = hex_char_val(s[2]) * 17;
        b = hex_char_val(s[3]) * 17;
    } else if (slen == 7 && s[0] == '#') {
        r = hex_char_val(s[1]) * 16 + hex_char_val(s[2]);
        g = hex_char_val(s[3]) * 16 + hex_char_val(s[4]);
        b = hex_char_val(s[5]) * 16 + hex_char_val(s[6]);
    } else {
        return -1;
    }
    int best = -1, best_dist = 999999999;
    for (int i = 0; i < 16; ++i) {
        int dr = r - (int)((vga_rgb[i] >> 16) & 0xFF);
        int dg = g - (int)((vga_rgb[i] >> 8) & 0xFF);
        int db = b - (int)(vga_rgb[i] & 0xFF);
        int d = dr * dr + dg * dg + db * db;
        if (d < best_dist) { best_dist = d; best = i; }
    }
    return best;
}

static int css_color_to_vga(const char *s, int slen) {
    if (slen <= 0) return -1;
    if (s[0] == '#') return hex_color_to_vga(s, slen);
    return color_name_to_vga(s, slen);
}

static void parse_style_attr(const char *tag, int tag_len, int *fg_out, int *bg_out, int *border_out) {
    int i = 0;
    int fg = -1, bg = -1, border = 0;
    while (i < tag_len) {
        while (i < tag_len && tag[i] != 's' && tag[i] != 'S' && tag[i] != 'b' && tag[i] != 'c') ++i;
        if (i + 5 >= tag_len) break;
        if ((tag[i] == 's' || tag[i] == 'S') &&
            (tag[i+1] == 't' || tag[i+1] == 'T') &&
            (tag[i+2] == 'y' || tag[i+2] == 'Y') &&
            (tag[i+3] == 'l' || tag[i+3] == 'L') &&
            tag[i+4] == '=') {
            i += 5;
            if (tag[i] == '"') ++i;
            int style_start = i;
            while (i < tag_len && tag[i] != '"' && tag[i] != '>') ++i;
            int style_end = i;
            int j = style_start;
            while (j < style_end) {
                while (j < style_end && (tag[j] == ' ' || tag[j] == ';')) ++j;
                if (j >= style_end) break;
                int prop_start = j;
                while (j < style_end && tag[j] != ':' && tag[j] != ';') ++j;
                int prop_end = j;
                while (j < style_end && tag[j] == ':') ++j;
                int val_start = j;
                while (j < style_end && tag[j] != ';' && tag[j] != ' ') ++j;
                int val_end = j;
                int plen = prop_end - prop_start;
                int vlen = val_end - val_start;
                if (plen == 5 && strncmp(tag + prop_start, "color", 5) == 0 && vlen > 0) {
                    int c = css_color_to_vga(tag + val_start, vlen);
                    if (c >= 0) fg = c;
                } else if ((plen == 10 && strncmp(tag + prop_start, "background", 10) == 0) ||
                           (plen == 17 && strncmp(tag + prop_start, "background-color", 17) == 0)) {
                    int c = css_color_to_vga(tag + val_start, vlen);
                    if (c >= 0) bg = c;
                } else if (plen == 6 && strncmp(tag + prop_start, "border", 6) == 0) {
                    border = 1;
                }
            }
            continue;
        }
        if ((tag[i] == 'c' || tag[i] == 'C') &&
            (tag[i+1] == 'o' || tag[i+1] == 'O') &&
            (tag[i+2] == 'l' || tag[i+2] == 'L') &&
            (tag[i+3] == 'o' || tag[i+3] == 'O') &&
            (tag[i+4] == 'r' || tag[i+4] == 'R') &&
            tag[i+5] == '=') {
            i += 6;
            if (tag[i] == '"') ++i;
            int vs = i;
            while (i < tag_len && tag[i] != '"' && tag[i] != '>' && tag[i] != ' ') ++i;
            int c = css_color_to_vga(tag + vs, i - vs);
            if (c >= 0) fg = c;
            continue;
        }
        if ((tag[i] == 'b' || tag[i] == 'B') &&
            (tag[i+1] == 'g' || tag[i+1] == 'G') &&
            (tag[i+2] == 'c' || tag[i+2] == 'C') &&
            (tag[i+3] == 'o' || tag[i+3] == 'O') &&
            (tag[i+4] == 'l' || tag[i+4] == 'L') &&
            (tag[i+5] == 'o' || tag[i+5] == 'O') &&
            (tag[i+6] == 'r' || tag[i+6] == 'R') &&
            tag[i+7] == '=') {
            i += 8;
            if (tag[i] == '"') ++i;
            int vs = i;
            while (i < tag_len && tag[i] != '"' && tag[i] != '>' && tag[i] != ' ') ++i;
            int c = css_color_to_vga(tag + vs, i - vs);
            if (c >= 0) bg = c;
            continue;
        }
        if ((tag[i] == 'b' || tag[i] == 'B') &&
            (tag[i+1] == 'o' || tag[i+1] == 'O') &&
            (tag[i+2] == 'r' || tag[i+2] == 'R') &&
            (tag[i+3] == 'd' || tag[i+3] == 'D') &&
            (tag[i+4] == 'e' || tag[i+4] == 'E') &&
            (tag[i+5] == 'r' || tag[i+5] == 'R') &&
            tag[i+6] == '=') {
            i += 7;
            if (tag[i] == '"') ++i;
            int vs = i;
            while (i < tag_len && tag[i] != '"' && tag[i] != '>' && tag[i] != ' ') ++i;
            if (i > vs && (tag[vs] == '1' || (tag[vs] >= '2' && tag[vs] <= '9')))
                border = 1;
            continue;
        }
        ++i;
    }
    if (fg_out) *fg_out = fg;
    if (bg_out) *bg_out = bg;
    if (border_out) *border_out = border;
}

static void html_strip(const char *html, u32 len) {
    g_line_count = 0;
    g_link_count = 0;
    g_sel_link = -1;

    char text[BROWSER_MAX_LINE];
    int tpos = 0;
    int cur_fg = -1, cur_bg = -1, cur_border = 0, cur_shadow = 0;
    int style_depth = 0;
    int border_depth[64], shadow_depth[64];
    int fg_stack[64], bg_stack[64];
    int stack_pos = 0;

    for (u32 i = 0; i < len && g_line_count < BROWSER_MAX_LINES; ++i) {
        char ch = html[i];

        if (ch == '<') {
            int tag_start = (int)i;
            int closing = (i + 1 < len && html[i + 1] == '/');
            int is_br = 0, is_p = 0, is_title = 0, is_a = 0;
            int is_div = 0, is_span = 0, is_table = 0, is_td = 0, is_th = 0;
            int is_h1 = 0, is_h2 = 0, is_h3 = 0, is_li = 0;

            char href[192];
            int hpos = 0;

            int j = (int)i + 1;
            if (html[j] == '/') ++j;
            while (j < (int)len && html[j] != '>' && html[j] != ' ') {
                char tc = html[j];
                if (tc >= 'A' && tc <= 'Z') tc = (char)(tc - 'A' + 'a');
                if (j == tag_start + 1 + (closing ? 1 : 0)) {
                    if (tc == 'b' && j + 1 < (int)len && html[j+1] == 'r') is_br = 1;
                    if (tc == 'p') is_p = 1;
                    if (tc == 'd' && j + 2 < (int)len && html[j+1] == 'i' && html[j+2] == 'v') is_div = 1;
                    if (tc == 's' && j + 3 < (int)len && html[j+1] == 'p' && html[j+2] == 'a' && html[j+3] == 'n') is_span = 1;
                    if (tc == 't' && j + 4 < (int)len && html[j+1] == 'a' && html[j+2] == 'b' && html[j+3] == 'l' && html[j+4] == 'e') is_table = 1;
                    if (tc == 't' && j + 1 < (int)len && html[j+1] == 'd') is_td = 1;
                    if (tc == 't' && j + 1 < (int)len && html[j+1] == 'h') is_th = 1;
                    if (tc == 't' && j + 4 < (int)len &&
                        html[j+1] == 'i' && html[j+2] == 't' && html[j+3] == 'l' && html[j+4] == 'e') is_title = 1;
                    if (tc == 'a') is_a = 1;
                    if (tc == 'h' && j + 1 < (int)len && html[j+1] == '1') is_h1 = 1;
                    if (tc == 'h' && j + 1 < (int)len && html[j+1] == '2') is_h2 = 1;
                    if (tc == 'h' && j + 1 < (int)len && html[j+1] == '3') is_h3 = 1;
                    if (tc == 'l' && j + 1 < (int)len && html[j+1] == 'i') is_li = 1;
                }
                ++j;
            }

            int tag_end = j;
            while (j < (int)len && html[j] != '>') ++j;
            (void)tag_start;
            int tag_has_closing = (j < (int)len && html[j] == '>') ? 1 : 0;

            if (!closing && tag_has_closing && (is_div || is_span || is_table || is_td || is_th)) {
                int fg = -1, bg = -1, border = 0;
                parse_style_attr(html + tag_start, tag_end - tag_start, &fg, &bg, &border);
                if (stack_pos < 64) {
                    fg_stack[stack_pos] = cur_fg;
                    bg_stack[stack_pos] = cur_bg;
                    border_depth[stack_pos] = cur_border;
                    shadow_depth[stack_pos] = cur_shadow;
                    ++stack_pos;
                }
                if (fg >= 0) cur_fg = fg;
                if (bg >= 0) cur_bg = bg;
                if (border) cur_border += border;
                ++style_depth;
            }

            if (closing && (is_div || is_span || is_table || is_td || is_th) && style_depth > 0 && stack_pos > 0) {
                --stack_pos;
                cur_fg = fg_stack[stack_pos];
                cur_bg = bg_stack[stack_pos];
                cur_border = border_depth[stack_pos];
                cur_shadow = shadow_depth[stack_pos];
                --style_depth;
            }

            if (is_a && !closing) {
                int k = tag_start + 1;
                while (k < j && html[k] != '>') {
                    if ((k + 5 < (int)len) && (html[k] == 'h' || html[k] == 'H') &&
                        (html[k+1] == 'r' || html[k+1] == 'R') &&
                        (html[k+2] == 'e' || html[k+2] == 'E') &&
                        (html[k+3] == 'f' || html[k+3] == 'F') &&
                        html[k+4] == '=') {
                        k += 5;
                        if (html[k] == '"') ++k;
                        while (k < (int)len && html[k] != '"' && html[k] != '>' && hpos < 190) {
                            href[hpos++] = html[k++];
                        }
                        href[hpos] = '\0';
                        break;
                    }
                    ++k;
                }
            }

            i = (u32)j;

            if (is_title) {
                ++i;
                while (i < len && html[i] != '<' && tpos < BROWSER_MAX_LINE - 1) {
                    char c = html[i];
                    if (c >= ' ') text[tpos++] = c;
                    ++i;
                }
                if (tpos > 0 && g_line_count < BROWSER_MAX_LINES) {
                    text[tpos] = '\0';
                    memcpy(g_lines[g_line_count], text, (usize)tpos + 1);
                    g_line_fg[g_line_count] = 15;
                    g_line_bg[g_line_count] = 0;
                    g_line_border[g_line_count] = 0;
                    g_line_shadow[g_line_count] = 0;
                    ++g_line_count;
                    tpos = 0;
                }
            }

            if ((is_br || is_p || is_li || is_h1 || is_h2 || is_h3) && tpos > 0) {
                text[tpos] = '\0';
                if (g_line_count < BROWSER_MAX_LINES) {
                    memcpy(g_lines[g_line_count], text, (usize)tpos + 1);
                    g_line_fg[g_line_count] = (u8)(cur_fg >= 0 ? cur_fg : 15);
                    g_line_bg[g_line_count] = (u8)(cur_bg >= 0 ? cur_bg : 0);
                    g_line_border[g_line_count] = cur_border;
                    g_line_shadow[g_line_count] = cur_shadow;
                    ++g_line_count;
                }
                tpos = 0;
            }
            if (is_p) {
                if (g_line_count < BROWSER_MAX_LINES) {
                    g_lines[g_line_count][0] = '\0';
                    g_line_fg[g_line_count] = (u8)(cur_fg >= 0 ? cur_fg : 15);
                    g_line_bg[g_line_count] = (u8)(cur_bg >= 0 ? cur_bg : 0);
                    g_line_border[g_line_count] = cur_border;
                    g_line_shadow[g_line_count] = cur_shadow;
                    ++g_line_count;
                }
            }
            if (is_li && tpos == 0) {
                text[tpos++] = '*';
                text[tpos] = '\0';
            }

            if (is_a && !closing && hpos > 0 && g_link_count < BROWSER_MAX_LINKS) {
                g_links[g_link_count].line = g_line_count;
                memcpy(g_links[g_link_count].url, href, (usize)hpos + 1);
                ++g_link_count;
            }
            continue;
        }

        if (ch == '\r') continue;
        if (ch == '\n') { ch = ' '; }
        if (ch >= ' ' && ch <= '~') {
            if (tpos < BROWSER_MAX_LINE - 1) text[tpos++] = ch;
        }
        if (tpos >= BROWSER_MAX_LINE - 1) {
            text[tpos] = '\0';
            if (g_line_count < BROWSER_MAX_LINES) {
                memcpy(g_lines[g_line_count], text, (usize)tpos + 1);
                g_line_fg[g_line_count] = (u8)(cur_fg >= 0 ? cur_fg : 15);
                g_line_bg[g_line_count] = (u8)(cur_bg >= 0 ? cur_bg : 0);
                g_line_border[g_line_count] = cur_border;
                g_line_shadow[g_line_count] = cur_shadow;
                ++g_line_count;
            }
            tpos = 0;
        }
    }

    if (tpos > 0 && g_line_count < BROWSER_MAX_LINES) {
        text[tpos] = '\0';
        memcpy(g_lines[g_line_count], text, (usize)tpos + 1);
        g_line_fg[g_line_count] = (u8)(cur_fg >= 0 ? cur_fg : 15);
        g_line_bg[g_line_count] = (u8)(cur_bg >= 0 ? cur_bg : 0);
        g_line_border[g_line_count] = cur_border;
        g_line_shadow[g_line_count] = cur_shadow;
        ++g_line_count;
    }

    int cols = brows_cols();
    for (int i = 0; i < g_line_count; ++i) {
        int ln = (int)strlen(g_lines[i]);
        if (ln > cols) g_lines[i][cols] = '\0';
    }
}

typedef enum {
    FETCH_IDLE,
    FETCH_LOADING,
    FETCH_PARSING,
    FETCH_READY,
    FETCH_ERROR
} BrowserFetchState;

static BrowserFetchState g_fetch_state = FETCH_IDLE;
static u8 g_fetch_buf[BROWSER_MAX_BYTES + 1];
static u32 g_fetch_out_len;

void browser_start(const char *url) {
    if (g_fetch_state == FETCH_LOADING) {
        net_async_http_abort();
    }
    g_fetch_state = FETCH_IDLE;

    strncpy(g_url, url, sizeof(g_url) - 1);
    g_url[sizeof(g_url) - 1] = '\0';
    g_line_count = 0;
    g_scroll = 0;
    g_sel_link = -1;
    g_active = 1;
    g_dirty = 1;

    if (net_async_http_start(g_url, g_fetch_buf, BROWSER_MAX_BYTES, &g_fetch_out_len)) {
        g_fetch_state = FETCH_LOADING;
    } else {
        g_fetch_state = FETCH_ERROR;
        if (g_line_count < BROWSER_MAX_LINES) {
            strcpy(g_lines[0], "browser: failed to fetch URL");
            g_line_fg[0] = 12;
            g_line_bg[0] = 0;
            g_line_border[0] = 0;
            g_line_shadow[0] = 0;
            g_line_count = 1;
        }
    }
    g_dirty = 1;
}

void browser_tick(void) {
    if (!g_active && g_fetch_state == FETCH_LOADING) {
        net_async_http_abort();
        g_fetch_state = FETCH_IDLE;
        return;
    }
    if (g_fetch_state != FETCH_LOADING) return;

    int status = net_async_http_poll();
    if (status == 0) return;

    if (status == 1) {
        g_fetch_state = FETCH_PARSING;
        if (g_fetch_out_len > 0) {
            g_fetch_buf[g_fetch_out_len] = '\0';
            html_strip((const char *)g_fetch_buf, g_fetch_out_len);
        }
        g_fetch_state = FETCH_READY;
    } else {
        g_fetch_state = FETCH_ERROR;
        g_line_count = 0;
        if (g_line_count < BROWSER_MAX_LINES) {
            strcpy(g_lines[0], "browser: failed to fetch URL");
            g_line_fg[0] = 12;
            g_line_bg[0] = 0;
            g_line_border[0] = 0;
            g_line_shadow[0] = 0;
            g_line_count = 1;
        }
    }
    g_dirty = 1;
}

int browser_active(void) { return g_active; }

int browser_needs_redraw(void) { return g_dirty; }

void browser_keyboard_event(u16 key, int pressed) {
    if (!g_active || !pressed) return;
    if (key == KEY_ESC || key == (0x200u | 'q')) {
        g_active = 0;
        if (g_fetch_state == FETCH_LOADING) {
            net_async_http_abort();
            g_fetch_state = FETCH_IDLE;
        }
        return;
    }
    if (g_fetch_state != FETCH_READY && g_fetch_state != FETCH_ERROR) return;
    if (key == KEY_UP && g_scroll > 0) { --g_scroll; g_dirty = 1; }
    else if (key == KEY_DOWN && g_scroll + brows_rows() < g_line_count) { ++g_scroll; g_dirty = 1; }
    else if (key == KEY_PGUP) { g_scroll -= brows_rows(); if (g_scroll < 0) g_scroll = 0; g_dirty = 1; }
    else if (key == KEY_PGDN) {
        g_scroll += brows_rows();
        if (g_scroll >= g_line_count) g_scroll = g_line_count - 1;
        g_dirty = 1;
    } else if (key == KEY_HOME) { g_scroll = 0; g_dirty = 1; }
    else if (key == KEY_END) { g_scroll = g_line_count - brows_rows(); if (g_scroll < 0) g_scroll = 0; g_dirty = 1; }
    else if (key == KEY_TAB) {
        if (g_link_count > 0) {
            if (g_sel_link < 0) g_sel_link = 0;
            else g_sel_link = (g_sel_link + 1) % g_link_count;
            g_dirty = 1;
        }
    } else if (key == KEY_ENTER && g_sel_link >= 0 && g_sel_link < g_link_count) {
        browser_start(g_links[g_sel_link].url);
    }
}

static void draw_shadow(int x, int y, int w, int h) {
    video_fill_rect(x + 4, y + 4, w, h, 0);
}

static void draw_border(int x, int y, int w, int h) {
    video_fill_rect(x, y, w, 1, 8);
    video_fill_rect(x, y + h - 1, w, 1, 8);
    video_fill_rect(x, y, 1, h, 8);
    video_fill_rect(x + w - 1, y, 1, h, 8);
}

void browser_render(void) {
    if (!g_active) return;
    int fh = video_font_height();
    int rows = brows_rows();
    int content_top = 16;
    int content_bot = (int)video_height() - 16;
    int content_h = content_bot - content_top;

    video_clear(0);
    video_fill_rect(0, 0, (int)video_width(), 16, 1);

    char hdr[256]; usize hl = 0;
    hdr[0] = '\0';
    app_text(hdr, sizeof(hdr), &hl, "BRW ");
    app_text(hdr, sizeof(hdr), &hl, g_url);
    video_draw_text(4, 4, 15, 1, hdr);

    if (g_fetch_state == FETCH_LOADING) {
        int cy = content_top + content_h / 2 - fh / 2;
        video_fill_rect(0, cy, (int)video_width(), fh, 0);
        video_draw_text(4, cy, 8, 0, "Loading...");
    }

    int text_x = 8;

    for (int r = 0; r < rows && g_scroll + r < g_line_count; ++r) {
        int li = g_scroll + r;
        int y = 16 + r * fh;
        int is_link = 0;
        int link_idx = -1;
        for (int l = 0; l < g_link_count; ++l) {
            if (g_links[l].line == li) { is_link = 1; link_idx = l; break; }
        }

        u8 bg = g_line_bg[li];
        u8 fg = g_line_fg[li];
        if (is_link && link_idx == g_sel_link) { bg = 15; fg = 0; }
        else if (is_link) { fg = 14; }

        int has_border = g_line_border[li];
        int has_shadow = g_line_shadow[li];
        int vcols = brows_cols();

        if (has_shadow)
            draw_shadow(text_x, y, vcols * 8, fh);

        if (has_border)
            draw_border(text_x - 2, y - 1, vcols * 8 + 4, fh + 2);

        video_fill_rect(0, y, (int)video_width(), fh, bg);
        char disp[BROWSER_MAX_LINE + 8];
        int dp = 0;

        if (is_link) {
            disp[dp++] = '[';
            if (link_idx < 10) disp[dp++] = (char)('0' + link_idx);
            else { disp[dp++] = (char)('0' + link_idx / 10); disp[dp++] = (char)('0' + link_idx % 10); }
            disp[dp++] = ']';
            disp[dp++] = ' ';
        }

        for (int i = 0; g_lines[li][i] && dp < vcols; ++i)
            disp[dp++] = g_lines[li][i];
        disp[dp] = '\0';

        video_draw_text(text_x, y, fg, bg, disp);
    }

    video_fill_rect(0, (int)video_height() - 16, (int)video_width(), 16, 1);
    {
        char st[128]; usize sl = 0;
        st[0] = '\0';
        app_text(st, sizeof(st), &sl, "Tab:select  Enter:follow  arrows:scroll  Ctrl+Q:back");
        video_draw_text(4, (int)video_height() - 12, 15, 1, st);
    }

    g_dirty = 0;
}
