#include "kernel.h"

#define SNAKE_HEADER_HEIGHT 16
#define SNAKE_BORDER 2
#define MAX_COLS 96
#define MAX_ROWS 64
#define MAX_CELLS (MAX_COLS * MAX_ROWS)

enum {
    STATE_SPLASH = 0,
    STATE_PLAY = 1,
    STATE_PAUSED = 2,
    STATE_GAME_OVER = 3,
};

enum {
    DIR_UP = 0,
    DIR_RIGHT = 1,
    DIR_DOWN = 2,
    DIR_LEFT = 3,
};

static int g_active, g_dirty, g_result_ready, g_state;
static int g_dir, g_next_dir, g_move_acc, g_tick_div;
static u16 g_len;
static u8 g_x[MAX_CELLS], g_y[MAX_CELLS];
static u8 g_fx, g_fy;
static u32 g_score, g_high_score, g_last_score, g_rng;
static u64 g_total_ram, g_req_ram;
static int g_cell_size, g_cols, g_rows, g_ox, g_oy;

static void append_char(char *b, usize sz, usize *len, char ch) {
    if (*len + 1 >= sz) return;
    b[*len] = ch; ++(*len); b[*len] = '\0';
}

static void append_text(char *b, usize sz, usize *len, const char *t) {
    while (*t) append_char(b, sz, len, *t++);
}

static void append_u32_dec(char *b, usize sz, usize *len, u32 v) {
    char d[16]; int c = 0;
    do { d[c++] = (char)('0' + (v % 10u)); v /= 10u; } while (v && c < 16);
    while (c > 0) append_char(b, sz, len, d[--c]);
}

static u32 rng_next(void) {
    g_rng = g_rng * 1664525u + 1013904223u + (u32)g_ticks;
    return g_rng;
}

static u32 ram_mb(u64 bytes) {
    return (u32)(bytes / (1024ull * 1024ull));
}

static int occupied(u8 x, u8 y, u16 lim) {
    for (u16 i = 0; i < lim; ++i)
        if (g_x[i] == x && g_y[i] == y) return 1;
    return 0;
}

static void place_food(void) {
    u16 max = (u16)(g_cols * g_rows);
    if (g_len >= max) return;
    for (u32 att = 0; att < 4096u; ++att) {
        u8 x = (u8)(rng_next() % g_cols);
        u8 y = (u8)(rng_next() % g_rows);
        if (!occupied(x, y, g_len)) { g_fx = x; g_fy = y; return; }
    }
    g_fx = 0; g_fy = 0;
}

static int cell_px(u8 x) { return g_ox + (int)x * g_cell_size; }
static int cell_py(u8 y) { return g_oy + (int)y * g_cell_size; }

static void clear_cell(u8 x, u8 y) {
    video_fill_rect(cell_px(x), cell_py(y), g_cell_size, g_cell_size, 0);
}

static void draw_cell(u8 x, u8 y, u8 color) {
    video_fill_rect(cell_px(x) + 1, cell_py(y) + 1, g_cell_size - 2, g_cell_size - 2, color);
}

static void draw_eyes(u8 x, u8 y, int dir) {
    int ox = cell_px(x), oy = cell_py(y);
    int e = g_cell_size / 4;
    int d = g_cell_size - e - 2;
    if (dir == DIR_UP) {
        video_fill_rect(ox + e, oy + 1, 2, 2, 15);
        video_fill_rect(ox + d, oy + 1, 2, 2, 15);
    } else if (dir == DIR_DOWN) {
        video_fill_rect(ox + e, oy + g_cell_size - 3, 2, 2, 15);
        video_fill_rect(ox + d, oy + g_cell_size - 3, 2, 2, 15);
    } else if (dir == DIR_RIGHT) {
        video_fill_rect(ox + g_cell_size - 3, oy + e, 2, 2, 15);
        video_fill_rect(ox + g_cell_size - 3, oy + d, 2, 2, 15);
    } else {
        video_fill_rect(ox + 1, oy + e, 2, 2, 15);
        video_fill_rect(ox + 1, oy + d, 2, 2, 15);
    }
}

static void draw_food(void) {
    draw_cell(g_fx, g_fy, 12);
    video_fill_rect(cell_px(g_fx) + g_cell_size / 2 - 1, cell_py(g_fy) + 1, 2, 2, 10);
}

static void draw_border(void) {
    int pw = g_cols * g_cell_size;
    int ph = g_rows * g_cell_size;
    video_fill_rect(g_ox - SNAKE_BORDER, g_oy - SNAKE_BORDER, pw + SNAKE_BORDER * 2, SNAKE_BORDER, 8);
    video_fill_rect(g_ox - SNAKE_BORDER, g_oy + ph, pw + SNAKE_BORDER * 2, SNAKE_BORDER, 8);
    video_fill_rect(g_ox - SNAKE_BORDER, g_oy, SNAKE_BORDER, ph, 8);
    video_fill_rect(g_ox + pw, g_oy, SNAKE_BORDER, ph, 8);
}

static void write_hdr(int row, const char *text) {
    video_draw_text(4, row * 8, 15, 1, text);
}

static void compute_grid(void) {
    int aw = (int)video_width();
    int ah = (int)video_height();

    if (aw <= 0 || ah <= 0) {
        g_cell_size = 8; g_cols = 40; g_rows = 23;
    } else {
        int tc = 40, tr = 23, cw = aw / tc, ch = (ah - SNAKE_HEADER_HEIGHT) / tr;
        g_cell_size = cw < ch ? cw : ch;
        if (g_cell_size < 8) g_cell_size = 8;
        if (g_cell_size > 32) g_cell_size = 32;
        g_cols = aw / g_cell_size;
        g_rows = (ah - SNAKE_HEADER_HEIGHT) / g_cell_size;
        if (g_cols < 10) g_cols = 10;
        if (g_rows < 10) g_rows = 10;
        if (g_cols > MAX_COLS) g_cols = MAX_COLS;
        if (g_rows > MAX_ROWS) g_rows = MAX_ROWS;
    }

    g_ox = ((int)video_width() - g_cols * g_cell_size) / 2;
    g_oy = SNAKE_HEADER_HEIGHT + ((int)video_height() - SNAKE_HEADER_HEIGHT - g_rows * g_cell_size) / 2;
}

static void reset_round(void) {
    g_len = 4; g_dir = DIR_RIGHT; g_next_dir = DIR_RIGHT;
    g_move_acc = 0; g_score = 0; g_tick_div = 6;
    int cx = g_cols / 2, cy = g_rows / 2;
    g_x[0] = (u8)cx; g_y[0] = (u8)cy;
    g_x[1] = (u8)(cx - 1); g_y[1] = (u8)cy;
    g_x[2] = (u8)(cx - 2); g_y[2] = (u8)cy;
    g_x[3] = (u8)(cx - 3); g_y[3] = (u8)cy;
    place_food();
}

static void finish_session(void) {
    g_active = 0; g_dirty = 0; g_result_ready = 1; g_last_score = g_score;
}

static void render_full(void) {
    char line[64]; usize len = 0;

    if (g_state == STATE_SPLASH) {
        video_clear(1);
        video_fill_rect(24, 24, 272, 152, 0);
        video_draw_text(104, 40, 14, 0, "flareOS snake");
        video_draw_text(56, 72, 15, 0, "warning before launch");

        line[0] = '\0';
        append_text(line, sizeof(line), &len, "minimum ram ");
        append_u32_dec(line, sizeof(line), &len, ram_mb(g_req_ram));
        append_text(line, sizeof(line), &len, " MiB");
        video_draw_text(56, 96, 10, 0, line);

        len = 0; line[0] = '\0';
        append_text(line, sizeof(line), &len, "detected ram ");
        append_u32_dec(line, sizeof(line), &len, ram_mb(g_total_ram));
        append_text(line, sizeof(line), &len, " MiB");
        video_draw_text(56, 112, 11, 0, line);

        if (g_high_score > 0) {
            len = 0; line[0] = '\0';
            append_text(line, sizeof(line), &len, "high score ");
            append_u32_dec(line, sizeof(line), &len, g_high_score);
            video_draw_text(56, 124, 14, 0, line);
        }

        video_draw_text(56, 144, 15, 0, "enter start   q cancel");
        video_draw_text(56, 160, 15, 0, "wasd move  p pause");
        g_dirty = 0;
        return;
    }

    video_clear(0);
    video_fill_rect(0, 0, (int)video_width(), SNAKE_HEADER_HEIGHT, 1);
    draw_border();
    write_hdr(0, "snake  wasd move  p pause  q shell");

    len = 0; line[0] = '\0';
    append_text(line, sizeof(line), &len, "score ");
    append_u32_dec(line, sizeof(line), &len, g_score);
    append_text(line, sizeof(line), &len, "  high ");
    append_u32_dec(line, sizeof(line), &len, g_high_score > g_score ? g_high_score : g_score);
    write_hdr(1, line);

    for (u16 i = 0; i < g_len; ++i)
        draw_cell(g_x[i], g_y[i], i == 0 ? 10 : 2);
    draw_food();
    draw_eyes(g_x[0], g_y[0], g_dir);

    if (g_state == STATE_PAUSED) {
        int cx = g_ox + g_cols * g_cell_size / 2;
        int cy = g_oy + g_rows * g_cell_size / 2;
        video_fill_rect(cx - 80, cy - 16, 160, 32, 4);
        video_draw_text(cx - 24, cy - 12, 15, 4, "PAUSED");
        video_draw_text(cx - 48, cy + 4, 15, 4, "p to resume");
    }

    if (g_state == STATE_GAME_OVER) {
        if (g_score > g_high_score) g_high_score = g_score;
        int cw = 200, ch = 56;
        int cx = g_ox + (g_cols * g_cell_size) / 2 - cw / 2;
        int cy = g_oy + (g_rows * g_cell_size) / 2 - ch / 2;
        video_fill_rect(cx, cy, cw, ch, 4);
        video_draw_text(cx + 48, cy + 8, 15, 4, "game over");

        len = 0; line[0] = '\0';
        append_text(line, sizeof(line), &len, "score ");
        append_u32_dec(line, sizeof(line), &len, g_score);
        video_draw_text(cx + 56, cy + 20, 15, 4, line);

        len = 0; line[0] = '\0';
        append_text(line, sizeof(line), &len, "high ");
        append_u32_dec(line, sizeof(line), &len, g_high_score);
        video_draw_text(cx + 56, cy + 32, 14, 4, line);
        video_draw_text(cx + 16, cy + 44, 15, 4, "enter restart  q shell");
    }

    g_dirty = 0;
}

void snake_start(u64 total_memory_bytes, u64 required_memory_bytes) {
    g_total_ram = total_memory_bytes;
    g_req_ram = required_memory_bytes;
    g_rng = (u32)(g_ticks ^ total_memory_bytes ^ required_memory_bytes);
    compute_grid();
    g_active = 1; g_dirty = 1; g_result_ready = 0;
    g_state = STATE_SPLASH; g_score = 0; g_last_score = 0;
}

int snake_active(void) { return g_active; }

void snake_keyboard_event(u16 key, int pressed) {
    if (!g_active || !pressed) return;

    if (g_state == STATE_SPLASH) {
        if (key == 'q' || key == KEY_ESC) finish_session();
        else if (key == KEY_ENTER || key == ' ') { reset_round(); g_state = STATE_PLAY; g_dirty = 1; }
        return;
    }

    if (g_state == STATE_GAME_OVER) {
        if (key == 'q' || key == KEY_ESC) finish_session();
        else if (key == KEY_ENTER || key == ' ') { reset_round(); g_state = STATE_PLAY; g_dirty = 1; }
        return;
    }

    if (key == 'q' || key == KEY_ESC) { finish_session(); return; }
    if (key == 'p') { g_state = g_state == STATE_PAUSED ? STATE_PLAY : STATE_PAUSED; g_dirty = 1; return; }
    if (g_state != STATE_PLAY) return;

    if (key == 'w' && g_dir != DIR_DOWN) g_next_dir = DIR_UP;
    else if (key == 'd' && g_dir != DIR_LEFT) g_next_dir = DIR_RIGHT;
    else if (key == 's' && g_dir != DIR_UP) g_next_dir = DIR_DOWN;
    else if (key == 'a' && g_dir != DIR_RIGHT) g_next_dir = DIR_LEFT;
}

void snake_timer_tick(void) {
    if (!g_active || g_state != STATE_PLAY) return;
    if (++g_move_acc < g_tick_div) return;
    g_move_acc = 0;

    u8 otx = g_x[g_len - 1], oty = g_y[g_len - 1];
    u8 ohx = g_x[0], ohy = g_y[0];

    g_dir = g_next_dir;
    int nx = g_x[0], ny = g_y[0];
    if (g_dir == DIR_UP) --ny;
    else if (g_dir == DIR_RIGHT) ++nx;
    else if (g_dir == DIR_DOWN) ++ny;
    else --nx;

    int grow = (nx == (int)g_fx && ny == (int)g_fy);
    if (nx < 0 || nx >= g_cols || ny < 0 || ny >= g_rows) { g_state = STATE_GAME_OVER; g_dirty = 1; return; }
    if (occupied((u8)nx, (u8)ny, grow ? g_len : (u16)(g_len - 1))) { g_state = STATE_GAME_OVER; g_dirty = 1; return; }

    u16 nl = grow ? (u16)(g_len + 1) : g_len;
    u16 max = (u16)(g_cols * g_rows);
    if (nl > max) nl = max;
    for (u16 i = nl - 1; i > 0; --i) { g_x[i] = g_x[i - 1]; g_y[i] = g_y[i - 1]; }
    g_x[0] = (u8)nx; g_y[0] = (u8)ny;

    if (!grow) clear_cell(otx, oty);
    draw_cell(ohx, ohy, 2);
    draw_cell((u8)nx, (u8)ny, 10);
    draw_eyes((u8)nx, (u8)ny, g_dir);

    g_len = nl;

    if (grow) {
        ++g_score;
        if (g_score % 5 == 0 && g_tick_div > 2) --g_tick_div;
        if (g_len >= max) { g_state = STATE_GAME_OVER; g_dirty = 1; return; }
        place_food();
        draw_food();
        g_dirty = 1;
    }
}

int snake_needs_redraw(void) { return g_dirty; }

void snake_render(void) { if (g_active) render_full(); }

int snake_consume_result(u32 *score_out) {
    if (!g_result_ready) return 0;
    g_result_ready = 0;
    if (score_out) *score_out = g_last_score;
    return 1;
}
