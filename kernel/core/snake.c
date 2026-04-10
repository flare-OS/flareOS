#include "kernel.h"

#define SNAKE_HEADER_HEIGHT 16
#define SNAKE_CELL_SIZE 8
#define SNAKE_COLS 40
#define SNAKE_ROWS 23
#define SNAKE_MAX_LEN (SNAKE_COLS * SNAKE_ROWS)
#define SNAKE_TICK_DIVIDER 6

enum {
    SNAKE_STATE_SPLASH = 0,
    SNAKE_STATE_PLAY = 1,
    SNAKE_STATE_GAME_OVER = 2,
};

enum {
    DIR_UP = 0,
    DIR_RIGHT = 1,
    DIR_DOWN = 2,
    DIR_LEFT = 3,
};

static int g_snake_active = 0;
static int g_snake_dirty = 0;
static int g_snake_result_ready = 0;
static int g_snake_state = SNAKE_STATE_SPLASH;
static int g_snake_direction = DIR_RIGHT;
static int g_snake_next_direction = DIR_RIGHT;
static u16 g_snake_length = 0;
static u8 g_snake_x[SNAKE_MAX_LEN];
static u8 g_snake_y[SNAKE_MAX_LEN];
static u8 g_food_x = 0;
static u8 g_food_y = 0;
static u32 g_score = 0;
static u32 g_last_score = 0;
static u32 g_rng_state = 0;
static u64 g_total_memory = 0;
static u64 g_required_memory = 0;
static int g_move_accum = 0;

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

static void append_u32_dec(char *buffer, usize size, usize *len, u32 value) {
    char digits[16];
    int count = 0;

    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value && count < (int)sizeof(digits));

    while (count > 0) {
        append_char(buffer, size, len, digits[--count]);
    }
}

static u32 random_next(void) {
    g_rng_state = g_rng_state * 1664525u + 1013904223u + (u32)g_ticks;
    return g_rng_state;
}

static u32 ram_mebibytes(u64 bytes) {
    return (u32)(bytes / (1024ull * 1024ull));
}

static int snake_occupied(u8 x, u8 y, u16 limit) {
    for (u16 i = 0; i < limit; ++i) {
        if (g_snake_x[i] == x && g_snake_y[i] == y) {
            return 1;
        }
    }
    return 0;
}

static void snake_place_food(void) {
    if (g_snake_length >= SNAKE_MAX_LEN) {
        return;
    }

    for (u32 attempt = 0; attempt < 4096u; ++attempt) {
        u8 x = (u8)(random_next() % SNAKE_COLS);
        u8 y = (u8)(random_next() % SNAKE_ROWS);

        if (!snake_occupied(x, y, g_snake_length)) {
            g_food_x = x;
            g_food_y = y;
            return;
        }
    }

    g_food_x = 0;
    g_food_y = 0;
}

static void snake_reset_round(void) {
    g_snake_length = 4;
    g_snake_direction = DIR_RIGHT;
    g_snake_next_direction = DIR_RIGHT;
    g_move_accum = 0;
    g_score = 0;

    g_snake_x[0] = 20;
    g_snake_y[0] = 10;
    g_snake_x[1] = 19;
    g_snake_y[1] = 10;
    g_snake_x[2] = 18;
    g_snake_y[2] = 10;
    g_snake_x[3] = 17;
    g_snake_y[3] = 10;

    snake_place_food();
}

static void snake_finish_session(void) {
    g_snake_active = 0;
    g_snake_dirty = 0;
    g_snake_result_ready = 1;
    g_last_score = g_score;
}

static void snake_draw_cell(u8 x, u8 y, u8 color) {
    int px = (int)x * SNAKE_CELL_SIZE + 1;
    int py = SNAKE_HEADER_HEIGHT + (int)y * SNAKE_CELL_SIZE + 1;

    video_fill_rect(px, py, SNAKE_CELL_SIZE - 2, SNAKE_CELL_SIZE - 2, color);
}

static void snake_draw_header_line(int row, const char *text) {
    video_draw_text(4, row * 8, 15, 1, text);
}

static void snake_render_splash(void) {
    char line[64];
    usize len = 0;

    video_clear(1);
    video_fill_rect(24, 24, 272, 152, 0);
    video_draw_text(104, 40, 14, 0, "flareOS snake");
    video_draw_text(56, 72, 15, 0, "warning before launch");

    line[0] = '\0';
    append_text(line, sizeof(line), &len, "minimum ram ");
    append_u32_dec(line, sizeof(line), &len, ram_mebibytes(g_required_memory));
    append_text(line, sizeof(line), &len, " MiB");
    video_draw_text(56, 96, 10, 0, line);

    len = 0;
    line[0] = '\0';
    append_text(line, sizeof(line), &len, "detected ram ");
    append_u32_dec(line, sizeof(line), &len, ram_mebibytes(g_total_memory));
    append_text(line, sizeof(line), &len, " MiB");
    video_draw_text(56, 112, 11, 0, line);

    video_draw_text(56, 136, 15, 0, "enter start   q cancel");
    video_draw_text(56, 152, 15, 0, "controls wasd");
}

static void snake_render_playfield(void) {
    char line[64];
    usize len = 0;

    video_clear(0);
    video_fill_rect(0, 0, (int)video_width(), SNAKE_HEADER_HEIGHT, 1);

    snake_draw_header_line(0, "snake  wasd move  q shell");
    line[0] = '\0';
    append_text(line, sizeof(line), &len, "score ");
    append_u32_dec(line, sizeof(line), &len, g_score);
    append_text(line, sizeof(line), &len, "  ram ");
    append_u32_dec(line, sizeof(line), &len, ram_mebibytes(g_total_memory));
    append_text(line, sizeof(line), &len, " MiB");
    snake_draw_header_line(1, line);

    for (u16 i = 0; i < g_snake_length; ++i) {
        snake_draw_cell(g_snake_x[i], g_snake_y[i], i == 0 ? 10 : 2);
    }
    snake_draw_cell(g_food_x, g_food_y, 12);
}

static void snake_render_game_over(void) {
    char line[48];
    usize len = 0;

    snake_render_playfield();
    video_fill_rect(64, 72, 192, 56, 4);
    video_draw_text(112, 80, 15, 4, "game over");
    line[0] = '\0';
    append_text(line, sizeof(line), &len, "score ");
    append_u32_dec(line, sizeof(line), &len, g_score);
    video_draw_text(112, 96, 15, 4, line);
    video_draw_text(72, 112, 15, 4, "enter restart  q shell");
}

void snake_start(u64 total_memory_bytes, u64 required_memory_bytes) {
    g_total_memory = total_memory_bytes;
    g_required_memory = required_memory_bytes;
    g_rng_state = (u32)(g_ticks ^ total_memory_bytes ^ required_memory_bytes);
    g_snake_active = 1;
    g_snake_dirty = 1;
    g_snake_result_ready = 0;
    g_snake_state = SNAKE_STATE_SPLASH;
    g_score = 0;
    g_last_score = 0;
}

int snake_active(void) {
    return g_snake_active;
}

void snake_keyboard_event(u16 key, int pressed) {
    if (!g_snake_active || !pressed) {
        return;
    }

    if (g_snake_state == SNAKE_STATE_SPLASH) {
        if (key == 'q' || key == KEY_ESC) {
            snake_finish_session();
        } else if (key == KEY_ENTER || key == ' ') {
            snake_reset_round();
            g_snake_state = SNAKE_STATE_PLAY;
            g_snake_dirty = 1;
        }
        return;
    }

    if (g_snake_state == SNAKE_STATE_GAME_OVER) {
        if (key == 'q' || key == KEY_ESC) {
            snake_finish_session();
        } else if (key == KEY_ENTER || key == ' ') {
            snake_reset_round();
            g_snake_state = SNAKE_STATE_PLAY;
            g_snake_dirty = 1;
        }
        return;
    }

    if (key == 'q' || key == KEY_ESC) {
        snake_finish_session();
        return;
    }
    if (key == 'w' && g_snake_direction != DIR_DOWN) {
        g_snake_next_direction = DIR_UP;
    } else if (key == 'd' && g_snake_direction != DIR_LEFT) {
        g_snake_next_direction = DIR_RIGHT;
    } else if (key == 's' && g_snake_direction != DIR_UP) {
        g_snake_next_direction = DIR_DOWN;
    } else if (key == 'a' && g_snake_direction != DIR_RIGHT) {
        g_snake_next_direction = DIR_LEFT;
    }
}

void snake_timer_tick(void) {
    int next_x;
    int next_y;
    int grow;
    u16 shift_len;

    if (!g_snake_active || g_snake_state != SNAKE_STATE_PLAY) {
        return;
    }

    if (++g_move_accum < SNAKE_TICK_DIVIDER) {
        return;
    }
    g_move_accum = 0;
    g_snake_direction = g_snake_next_direction;
    next_x = g_snake_x[0];
    next_y = g_snake_y[0];

    if (g_snake_direction == DIR_UP) {
        --next_y;
    } else if (g_snake_direction == DIR_RIGHT) {
        ++next_x;
    } else if (g_snake_direction == DIR_DOWN) {
        ++next_y;
    } else {
        --next_x;
    }

    grow = (next_x == (int)g_food_x && next_y == (int)g_food_y);
    if (next_x < 0 || next_x >= SNAKE_COLS || next_y < 0 || next_y >= SNAKE_ROWS) {
        g_snake_state = SNAKE_STATE_GAME_OVER;
        g_snake_dirty = 1;
        return;
    }
    if (snake_occupied((u8)next_x, (u8)next_y, grow ? g_snake_length : (u16)(g_snake_length - 1))) {
        g_snake_state = SNAKE_STATE_GAME_OVER;
        g_snake_dirty = 1;
        return;
    }

    shift_len = grow ? (u16)(g_snake_length + 1) : g_snake_length;
    if (shift_len > SNAKE_MAX_LEN) {
        shift_len = SNAKE_MAX_LEN;
    }
    for (u16 i = shift_len - 1; i > 0; --i) {
        g_snake_x[i] = g_snake_x[i - 1];
        g_snake_y[i] = g_snake_y[i - 1];
    }
    g_snake_x[0] = (u8)next_x;
    g_snake_y[0] = (u8)next_y;
    g_snake_length = shift_len;

    if (grow) {
        ++g_score;
        if (g_snake_length >= SNAKE_MAX_LEN) {
            g_snake_state = SNAKE_STATE_GAME_OVER;
        } else {
            snake_place_food();
        }
    }
    g_snake_dirty = 1;
}

int snake_needs_redraw(void) {
    return g_snake_dirty;
}

void snake_render(void) {
    if (!g_snake_active) {
        return;
    }

    if (g_snake_state == SNAKE_STATE_SPLASH) {
        snake_render_splash();
    } else if (g_snake_state == SNAKE_STATE_GAME_OVER) {
        snake_render_game_over();
    } else {
        snake_render_playfield();
    }
    g_snake_dirty = 0;
}

int snake_consume_result(u32 *score_out) {
    if (!g_snake_result_ready) {
        return 0;
    }
    g_snake_result_ready = 0;
    if (score_out) {
        *score_out = g_last_score;
    }
    return 1;
}
