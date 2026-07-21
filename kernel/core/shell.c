#include "kernel.h"
#include "x86_64.h"

#define SHELL_SESSION_COUNT 4
#define SHELL_MAX_LINES 128
#define SHELL_MAX_LINE_LEN 96
#define SHELL_INPUT_LEN 80
#define SHELL_FILE_NAME_LEN 48
#define SNAKE_MIN_RAM_BYTES (8ull * 1024ull * 1024ull)

enum {
    SHELL_VIEW_SESSION = 0,
    SHELL_VIEW_SWITCHER = 1,
    SHELL_VIEW_TASKMAN = 2,
    SHELL_VIEW_INSTALLER = 3,
};

typedef struct {
    char user[12];
    char lines[SHELL_MAX_LINES][SHELL_MAX_LINE_LEN];
    int line_count;
    char input[SHELL_INPUT_LEN];
    int input_len;
    u64 started_tick;
    u64 last_input_tick;
    u64 last_render_tick;
    u64 focus_ticks;
    u32 render_count;
    u32 key_count;
    u32 command_count;
    u32 switch_count;
} ShellSession;

static ShellSession g_sessions[SHELL_SESSION_COUNT];
static int g_active_session = 0;
static int g_selected_session = 0;
static int g_shell_view = SHELL_VIEW_SESSION;
static int g_installer_layout = KEYBOARD_LAYOUT_QWERTY;
static int g_snake_owner_session = -1;
static u64 g_snake_ticks = 0;
static u32 g_snake_input_count = 0;
static u32 g_snake_render_count = 0;
static int g_shell_dirty = 1;
static int g_prompt_dirty = 0;

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

static void append_spaces(char *buffer, usize size, usize *len, int count) {
    for (int i = 0; i < count; ++i) {
        append_char(buffer, size, len, ' ');
    }
}

static void append_padded_text(char *buffer, usize size, usize *len, const char *text, int width) {
    int written = 0;

    while (*text && written < width) {
        append_char(buffer, size, len, *text++);
        ++written;
    }
    while (written < width) {
        append_char(buffer, size, len, ' ');
        ++written;
    }
}

static void append_u64_dec(char *buffer, usize size, usize *len, u64 value) {
    char digits[24];
    int count = 0;

    do {
        digits[count++] = (char)('0' + (value % 10ull));
        value /= 10ull;
    } while (value && count < (int)sizeof(digits));

    while (count > 0) {
        append_char(buffer, size, len, digits[--count]);
    }
}

static void append_u64_dec_padded(char *buffer, usize size, usize *len, u64 value, int width) {
    char digits[24];
    int count = 0;
    int pad;

    do {
        digits[count++] = (char)('0' + (value % 10ull));
        value /= 10ull;
    } while (value && count < (int)sizeof(digits));

    pad = width - count;
    if (pad > 0) {
        append_spaces(buffer, size, len, pad);
    }
    while (count > 0) {
        append_char(buffer, size, len, digits[--count]);
    }
}

static u32 timer_hz(void) {
    return g_boot_info.timer_hz ? g_boot_info.timer_hz : 60u;
}

static u64 ticks_to_seconds(u64 ticks) {
    return ticks / timer_hz();
}

static u64 ram_mebibytes(u64 bytes) {
    return bytes / (1024ull * 1024ull);
}

static int body_cols(void) {
    int cols = console_width() - 2;
    return cols > 12 ? cols : 12;
}

static int body_rows(void) {
    int rows = console_height() - 3;
    return rows > 4 ? rows : 4;
}

static ShellSession *active_session(void) {
    return &g_sessions[g_active_session];
}

static int snake_visible_for_active_session(void) {
    return g_shell_view == SHELL_VIEW_SESSION && snake_active() && g_snake_owner_session == g_active_session;
}

static void session_push_line_raw(ShellSession *session, const char *text) {
    if (session->line_count == SHELL_MAX_LINES) {
        for (int i = 1; i < SHELL_MAX_LINES; ++i) {
            strcpy(session->lines[i - 1], session->lines[i]);
        }
        session->line_count = SHELL_MAX_LINES - 1;
    }
    strncpy(session->lines[session->line_count], text, SHELL_MAX_LINE_LEN - 1);
    session->lines[session->line_count][SHELL_MAX_LINE_LEN - 1] = '\0';
    ++session->line_count;
}

static void session_push_line(ShellSession *session, const char *text) {
    int cols = body_cols();
    usize len = strlen(text);
    usize offset = 0;

    if (len == 0) {
        session_push_line_raw(session, "");
        return;
    }

    while (offset < len) {
        char chunk[SHELL_MAX_LINE_LEN];
        usize count = 0;

        while (offset < len && count + 1 < sizeof(chunk) && (int)count < cols) {
            chunk[count++] = text[offset++];
        }
        chunk[count] = '\0';
        session_push_line_raw(session, chunk);
    }
}

static void session_prompt_line(const ShellSession *session, int session_index, char *buffer, usize size) {
    usize len = 0;

    buffer[0] = '\0';
    append_text(buffer, size, &len, session->user);
    append_char(buffer, size, &len, '@');
    append_u64_dec(buffer, size, &len, (u64)(session_index + 1));
    append_text(buffer, size, &len, "> ");
    append_text(buffer, size, &len, session->input);
}

static const char *session_state_label(int session_index) {
    if (snake_active() && g_snake_owner_session == session_index) {
        if (g_shell_view == SHELL_VIEW_SESSION && g_active_session == session_index) {
            return "snk";
        }
        return "hold";
    }
    if (g_active_session == session_index) {
        if (g_shell_view == SHELL_VIEW_TASKMAN) {
            return "top";
        }
        if (g_shell_view == SHELL_VIEW_SWITCHER) {
            return "pick";
        }
        return "fg";
    }
    return "bg";
}

static void build_bar(char *buffer, usize size, u64 used, u64 total, int width) {
    usize len = 0;
    int fill = 0;

    if (total != 0) {
        fill = (int)((used * (u64)width) / total);
    }
    if (fill < 0) {
        fill = 0;
    }
    if (fill > width) {
        fill = width;
    }

    buffer[0] = '\0';
    append_char(buffer, size, &len, '[');
    for (int i = 0; i < width; ++i) {
        append_char(buffer, size, &len, i < fill ? '#' : '.');
    }
    append_char(buffer, size, &len, ']');
}

static void shell_open_switcher(void) {
    g_shell_view = SHELL_VIEW_SWITCHER;
    g_selected_session = g_active_session;
    g_shell_dirty = 1;
}

static void shell_open_task_manager(void) {
    g_shell_view = SHELL_VIEW_TASKMAN;
    g_shell_dirty = 1;
}

static void shell_open_installer(void) {
    g_shell_view = SHELL_VIEW_INSTALLER;
    g_installer_layout = keyboard_layout();
    g_shell_dirty = 1;
}

static void shell_set_active_session(int session_index) {
    if (session_index < 0 || session_index >= SHELL_SESSION_COUNT) {
        return;
    }

    g_active_session = session_index;
    g_selected_session = session_index;
    ++g_sessions[session_index].switch_count;
    g_shell_view = SHELL_VIEW_SESSION;
    g_shell_dirty = 1;
}

static int session_index_from_target(const char *target) {
    while (*target == ' ') {
        ++target;
    }
    if (target[0] >= '1' && target[0] < '1' + SHELL_SESSION_COUNT && target[1] == '\0') {
        return target[0] - '1';
    }
    for (int i = 0; i < SHELL_SESSION_COUNT; ++i) {
        if (strcmp(target, g_sessions[i].user) == 0) {
            return i;
        }
    }
    return -1;
}

static const char *skip_spaces(const char *text) {
    while (*text == ' ') {
        ++text;
    }
    return text;
}

static const char *token_end(const char *text) {
    while (*text && *text != ' ') {
        ++text;
    }
    return text;
}

static int copy_token(const char *src, char *dest, usize size) {
    const char *end = token_end(src);
    usize len = (usize)(end - src);

    if (len == 0 || len >= size) {
        return 0;
    }
    memcpy(dest, src, len);
    dest[len] = '\0';
    return 1;
}

static void shell_report_layout(ShellSession *session) {
    char line[48];
    usize len = 0;

    line[0] = '\0';
    append_text(line, sizeof(line), &len, "layout ");
    append_text(line, sizeof(line), &len, keyboard_layout_name());
    session_push_line(session, line);
}

static void shell_report_root(ShellSession *session) {
    char line[64];
    usize len = 0;

    line[0] = '\0';
    append_text(line, sizeof(line), &len, "root ");
    append_text(line, sizeof(line), &len, fs_backend_name());
    append_text(line, sizeof(line), &len, fs_is_writable() ? " rw" : " ro");
    session_push_line(session, line);
}

static void shell_set_layout_command(ShellSession *session, const char *name) {
    if (strcmp(name, "azerty") == 0) {
        keyboard_set_layout(KEYBOARD_LAYOUT_AZERTY);
        g_installer_layout = KEYBOARD_LAYOUT_AZERTY;
        shell_report_layout(session);
        return;
    }
    if (strcmp(name, "qwerty") == 0) {
        keyboard_set_layout(KEYBOARD_LAYOUT_QWERTY);
        g_installer_layout = KEYBOARD_LAYOUT_QWERTY;
        shell_report_layout(session);
        return;
    }
    session_push_line(session, "layout must be qwerty or azerty");
}

static void shell_trigger_panic(void) {
    volatile u64 *garbage = (volatile u64 *)(usize)0x400000000ull;

    *garbage = 0xdeadbeefcafebabeull;
}

static void shell_print_uptime(ShellSession *session) {
    char line[64];
    usize len = 0;

    line[0] = '\0';
    append_text(line, sizeof(line), &len, "uptime ");
    append_u64_dec(line, sizeof(line), &len, ticks_to_seconds(g_ticks));
    append_text(line, sizeof(line), &len, "s");
    session_push_line(session, line);
}

static void shell_print_memory(ShellSession *session) {
    char line[96];
    usize len = 0;

    line[0] = '\0';
    append_text(line, sizeof(line), &len, "heap=");
    append_u64_dec(line, sizeof(line), &len, heap_bytes_used());
    append_text(line, sizeof(line), &len, " ram=");
    append_u64_dec(line, sizeof(line), &len, ram_mebibytes(g_boot_info.total_memory));
    append_text(line, sizeof(line), &len, "MiB map=");
    append_u64_dec(line, sizeof(line), &len, paging_mapped_bytes() / 4096u);
    append_text(line, sizeof(line), &len, "pg");
    session_push_line(session, line);
}

static void shell_print_uname(ShellSession *session) {
    session_push_line(session, "flareOS x86_64 multi-session shell");
}

static void shell_print_paging(ShellSession *session) {
    char line[96];
    usize len = 0;

    line[0] = '\0';
    append_text(line, sizeof(line), &len, "tables=");
    append_u64_dec(line, sizeof(line), &len, paging_table_count());
    append_text(line, sizeof(line), &len, " mapped=");
    append_u64_dec(line, sizeof(line), &len, paging_mapped_bytes() / 4096u);
    append_text(line, sizeof(line), &len, " pages");
    session_push_line(session, line);
}

static void shell_list_files(ShellSession *session) {
    if (fs_file_count() == 0) {
        session_push_line(session, "no files");
        return;
    }
    for (usize i = 0; i < fs_file_count(); ++i) {
        session_push_line(session, fs_file_name(i));
    }
}

static void shell_cat_file(ShellSession *session, const char *name) {
    for (usize i = 0; i < fs_file_count(); ++i) {
        const char *file_name = fs_file_name(i);
        if (strcmp(file_name, name) == 0) {
            usize size = 0;
            const char *data = fs_file_data(i, &size);
            char line[SHELL_MAX_LINE_LEN];
            usize line_len = 0;

            for (usize idx = 0; idx < size; ++idx) {
                char ch = data[idx];

                if (ch == '\r') {
                    continue;
                }
                if (ch == '\n') {
                    line[line_len] = '\0';
                    session_push_line(session, line);
                    line_len = 0;
                    continue;
                }
                if (ch < ' ' || ch > '~') {
                    ch = '.';
                }
                if (line_len + 1 >= sizeof(line)) {
                    line[line_len] = '\0';
                    session_push_line(session, line);
                    line_len = 0;
                }
                line[line_len++] = ch;
            }

            line[line_len] = '\0';
            if (line_len > 0 || size == 0) {
                session_push_line(session, line);
            }
            return;
        }
    }
    session_push_line(session, "file not found");
}

static void shell_list_users(ShellSession *session) {
    for (int i = 0; i < SHELL_SESSION_COUNT; ++i) {
        char line[48];
        usize len = 0;

        line[0] = '\0';
        append_text(line, sizeof(line), &len, "tty");
        append_u64_dec(line, sizeof(line), &len, (u64)(i + 1));
        append_char(line, sizeof(line), &len, ' ');
        append_text(line, sizeof(line), &len, g_sessions[i].user);
        if (i == g_active_session) {
            append_text(line, sizeof(line), &len, " *");
        }
        session_push_line(session, line);
    }
}

static void shell_write_file_command(ShellSession *session, const char *args, int append) {
    char name[SHELL_FILE_NAME_LEN];
    const char *tail = skip_spaces(args);
    const char *body;

    if (!fs_is_writable()) {
        session_push_line(session, "root fs is read-only");
        return;
    }
    if (!copy_token(tail, name, sizeof(name))) {
        session_push_line(session, append ? "usage: append <file> <text>" : "usage: write <file> <text>");
        return;
    }

    body = token_end(tail);
    body = skip_spaces(body);
    if (!fs_write_file(name, body, strlen(body), append)) {
        session_push_line(session, "write failed");
        return;
    }
    session_push_line(session, append ? "file appended" : "file written");
}

static void shell_remove_file_command(ShellSession *session, const char *args) {
    char name[SHELL_FILE_NAME_LEN];

    if (!fs_is_writable()) {
        session_push_line(session, "root fs is read-only");
        return;
    }
    if (!copy_token(skip_spaces(args), name, sizeof(name))) {
        session_push_line(session, "usage: rm <file>");
        return;
    }
    if (!fs_remove_file(name)) {
        session_push_line(session, "remove failed");
        return;
    }
    session_push_line(session, "file removed");
}

static void append_ip(char *buffer, usize size, usize *len, u32 ip) {
    append_u64_dec(buffer, size, len, (ip >> 24) & 0xFF);
    append_char(buffer, size, len, '.');
    append_u64_dec(buffer, size, len, (ip >> 16) & 0xFF);
    append_char(buffer, size, len, '.');
    append_u64_dec(buffer, size, len, (ip >> 8) & 0xFF);
    append_char(buffer, size, len, '.');
    append_u64_dec(buffer, size, len, ip & 0xFF);
}

static void shell_net_status(ShellSession *session) {
    char line[64];
    usize len = 0;
    line[0] = '\0';
    append_text(line, sizeof(line), &len, net_is_ready() ? "net: online" : "net: offline");
    session_push_line(session, line);
    if (net_is_ready()) {
        u32 ip, gw, dns, mask;
        net_get_ip(&ip, &gw, &dns, &mask);
        len = 0; line[0] = '\0';
        append_text(line, sizeof(line), &len, "ip ");
        append_ip(line, sizeof(line), &len, ip);
        session_push_line(session, line);
        len = 0; line[0] = '\0';
        append_text(line, sizeof(line), &len, "gw ");
        append_ip(line, sizeof(line), &len, gw);
        session_push_line(session, line);
        len = 0; line[0] = '\0';
        append_text(line, sizeof(line), &len, "dns ");
        append_ip(line, sizeof(line), &len, dns);
        session_push_line(session, line);
        len = 0; line[0] = '\0';
        append_text(line, sizeof(line), &len, "mask ");
        append_ip(line, sizeof(line), &len, mask);
        session_push_line(session, line);
    }
}

static void shell_curl(ShellSession *session, const char *url) {
    if (!net_is_ready()) {
        session_push_line(session, "net: network not ready");
        return;
    }
    session_push_line(session, "curl: fetching...");
    char name[32];
    usize nlen = 0;
    name[0] = '\0';
    append_text(name, sizeof(name), &nlen, "resp");
    {
        u32 h = 0;
        const char *p = url;
        while (*p) { h = h * 31 + *p; ++p; }
        append_u64_dec(name, sizeof(name), &nlen, h % 1000);
    }
    name[nlen] = '\0';

    u8 buf[8192];
    u32 out_len = 0;
    if (net_http_get(url, buf, sizeof(buf), &out_len)) {
        char size_line[64];
        usize slen = 0; size_line[0] = '\0';
        append_text(size_line, sizeof(size_line), &slen, "curl: got ");
        append_u64_dec(size_line, sizeof(size_line), &slen, out_len);
        append_text(size_line, sizeof(size_line), &slen, " bytes");
        session_push_line(session, size_line);
        if (fs_is_writable()) {
            if (fs_write_file(name, (const char *)buf, out_len, 0)) {
                char saved[64];
                usize saved_len = 0; saved[0] = '\0';
                append_text(saved, sizeof(saved), &saved_len, "curl: saved to ");
                append_text(saved, sizeof(saved), &saved_len, name);
                session_push_line(session, saved);
            } else {
                session_push_line(session, "curl: write failed");
            }
        } else {
            session_push_line(session, "curl: read-only fs");
        }
    } else {
        session_push_line(session, "curl: failed");
    }
}

static void shell_halt(ShellSession *session) {
    session_push_line(session, "cpu halted");
    g_shell_dirty = 1;
    shell_render();
    cli();
    for (;;) {
        hlt();
    }
}

static void shell_launch_snake(ShellSession *session) {
    if (g_boot_info.total_memory == 0) {
        session_push_line(session, "snake needs detected system RAM");
        return;
    }
    if (g_boot_info.total_memory < SNAKE_MIN_RAM_BYTES) {
        char line[96];
        usize len = 0;

        line[0] = '\0';
        append_text(line, sizeof(line), &len, "snake needs at least ");
        append_u64_dec(line, sizeof(line), &len, SNAKE_MIN_RAM_BYTES / (1024ull * 1024ull));
        append_text(line, sizeof(line), &len, " MiB ram");
        session_push_line(session, line);

        len = 0;
        line[0] = '\0';
        append_text(line, sizeof(line), &len, "detected ");
        append_u64_dec(line, sizeof(line), &len, g_boot_info.total_memory / (1024ull * 1024ull));
        append_text(line, sizeof(line), &len, " MiB");
        session_push_line(session, line);
        return;
    }
    if (snake_active() && g_snake_owner_session != g_active_session) {
        char line[64];
        usize len = 0;

        line[0] = '\0';
        append_text(line, sizeof(line), &len, "snake already running in tty");
        append_u64_dec(line, sizeof(line), &len, (u64)(g_snake_owner_session + 1));
        session_push_line(session, line);
        return;
    }

    g_snake_owner_session = g_active_session;
    g_snake_ticks = 0;
    g_snake_input_count = 0;
    g_snake_render_count = 0;
    snake_start(g_boot_info.total_memory, SNAKE_MIN_RAM_BYTES);
}

static void shell_render_task_row(int row,
                                  u32 pid,
                                  const char *user,
                                  const char *kind,
                                  const char *state,
                                  u64 ticks,
                                  u32 inputs,
                                  u32 commands,
                                  u32 renders,
                                  u8 attr) {
    char line[64];
    usize len = 0;

    line[0] = '\0';
    append_u64_dec_padded(line, sizeof(line), &len, pid, 3);
    append_char(line, sizeof(line), &len, ' ');
    append_padded_text(line, sizeof(line), &len, user, 4);
    append_char(line, sizeof(line), &len, ' ');
    append_padded_text(line, sizeof(line), &len, kind, 3);
    append_char(line, sizeof(line), &len, ' ');
    append_padded_text(line, sizeof(line), &len, state, 4);
    append_char(line, sizeof(line), &len, ' ');
    append_u64_dec_padded(line, sizeof(line), &len, ticks, 5);
    append_char(line, sizeof(line), &len, ' ');
    append_u64_dec_padded(line, sizeof(line), &len, inputs, 3);
    append_char(line, sizeof(line), &len, ' ');
    append_u64_dec_padded(line, sizeof(line), &len, commands, 3);
    append_char(line, sizeof(line), &len, ' ');
    append_u64_dec_padded(line, sizeof(line), &len, renders, 3);

    console_write_at(1, row, attr, line);
}

static void shell_render_session_view(void) {
    ShellSession *session = active_session();
    int visible_lines = body_rows();
    int start = session->line_count > visible_lines ? session->line_count - visible_lines : 0;
    char header[64];
    char status[128];
    char prompt[SHELL_INPUT_LEN + 20];
    usize len = 0;

    if (snake_visible_for_active_session()) {
        snake_render();
        ++session->render_count;
        ++g_snake_render_count;
        session->last_render_tick = g_ticks;
        g_shell_dirty = 0;
        return;
    }

    console_clear(0x01);
    console_fill_row(0, 0x1f);
    console_fill_row(1, 0x87);
    console_fill_row(console_height() - 1, 0x08);

    header[0] = '\0';
    append_text(header, sizeof(header), &len, "flareOS / tty");
    append_u64_dec(header, sizeof(header), &len, (u64)(g_active_session + 1));
    append_text(header, sizeof(header), &len, " / ");
    append_text(header, sizeof(header), &len, session->user);
    console_write_at(1, 0, 0x1f, header);

    len = 0;
    status[0] = '\0';
    append_text(status, sizeof(status), &len, "F1-F4 tty  F10 top  esc pick  ");
    append_text(status, sizeof(status), &len, fs_backend_name());
    append_char(status, sizeof(status), &len, ' ');
    append_text(status, sizeof(status), &len, fs_is_writable() ? "rw" : "ro");
    append_char(status, sizeof(status), &len, ' ');
    append_text(status, sizeof(status), &len, keyboard_layout_name());
    console_write_at(1, 1, 0x87, status);

    for (int row = 0; row < visible_lines && start + row < session->line_count; ++row) {
        console_fill_row(2 + row, 0x01);
        console_write_at(1, 2 + row, 0x0f, session->lines[start + row]);
    }

    session_prompt_line(session, g_active_session, prompt, sizeof(prompt));
    console_write_at(0, console_height() - 1, 0x0f, prompt);
    console_set_cursor((int)strlen(prompt), console_height() - 1);

    ++session->render_count;
    session->last_render_tick = g_ticks;
    g_shell_dirty = 0;
}

static void shell_render_switcher(void) {
    char line[96];
    usize len;

    console_clear(0x01);
    console_fill_row(0, 0x1f);
    console_fill_row(1, 0x87);
    console_write_at(1, 0, 0x1f, "flareOS / session switcher");
    console_write_at(1, 1, 0x87, "1-4 jump  enter attach  q back");

    for (int i = 0; i < SHELL_SESSION_COUNT; ++i) {
        ShellSession *session = &g_sessions[i];
        int row = 3 + i * 4;
        u8 line_attr = i == g_selected_session ? 0x1e : 0x0f;

        console_fill_row(row, i == g_selected_session ? 0x10 : 0x01);
        console_fill_row(row + 1, 0x01);
        console_fill_row(row + 2, 0x01);

        len = 0;
        line[0] = '\0';
        append_text(line, sizeof(line), &len, "tty");
        append_u64_dec(line, sizeof(line), &len, (u64)(i + 1));
        append_char(line, sizeof(line), &len, ' ');
        append_text(line, sizeof(line), &len, session->user);
        append_text(line, sizeof(line), &len, "  ");
        append_text(line, sizeof(line), &len, session_state_label(i));
        if (i == g_active_session) {
            append_text(line, sizeof(line), &len, "  current");
        }
        console_write_at(1, row, line_attr, line);

        len = 0;
        line[0] = '\0';
        append_text(line, sizeof(line), &len, "cmd=");
        append_u64_dec(line, sizeof(line), &len, session->command_count);
        append_text(line, sizeof(line), &len, " key=");
        append_u64_dec(line, sizeof(line), &len, session->key_count);
        append_text(line, sizeof(line), &len, " draw=");
        append_u64_dec(line, sizeof(line), &len, session->render_count);
        console_write_at(2, row + 1, 0x07, line);

        len = 0;
        line[0] = '\0';
        append_text(line, sizeof(line), &len, "focus=");
        append_u64_dec(line, sizeof(line), &len, ticks_to_seconds(session->focus_ticks));
        append_text(line, sizeof(line), &len, "s last=");
        append_u64_dec(line, sizeof(line), &len, ticks_to_seconds(g_ticks - session->last_input_tick));
        append_text(line, sizeof(line), &len, "s sw=");
        append_u64_dec(line, sizeof(line), &len, session->switch_count);
        console_write_at(2, row + 2, 0x07, line);
    }

    g_shell_dirty = 0;
}

static void shell_render_task_manager(void) {
    char line[96];
    char bar[32];
    usize len = 0;
    int row = 5;

    console_clear(0x01);
    console_fill_row(0, 0x1f);
    console_fill_row(1, 0x87);
    console_write_at(1, 0, 0x1f, "flareOS / taskman");

    line[0] = '\0';
    append_text(line, sizeof(line), &len, "tty");
    append_u64_dec(line, sizeof(line), &len, (u64)(g_active_session + 1));
    append_text(line, sizeof(line), &len, " ");
    append_text(line, sizeof(line), &len, g_sessions[g_active_session].user);
    append_text(line, sizeof(line), &len, "  q back  F1-F4 tty");
    console_write_at(1, 1, 0x87, line);

    build_bar(bar, sizeof(bar), g_boot_info.total_memory ? heap_bytes_used() : 0, g_boot_info.total_memory ? g_boot_info.total_memory : 1, 16);
    len = 0;
    line[0] = '\0';
    append_text(line, sizeof(line), &len, "heap ");
    append_text(line, sizeof(line), &len, bar);
    append_char(line, sizeof(line), &len, ' ');
    append_u64_dec(line, sizeof(line), &len, ram_mebibytes(heap_bytes_used()));
    append_text(line, sizeof(line), &len, "M");
    console_write_at(1, 2, 0x0f, line);

    build_bar(bar, sizeof(bar), paging_mapped_bytes(), g_boot_info.total_memory ? g_boot_info.total_memory : 1, 16);
    len = 0;
    line[0] = '\0';
    append_text(line, sizeof(line), &len, "map  ");
    append_text(line, sizeof(line), &len, bar);
    append_char(line, sizeof(line), &len, ' ');
    append_u64_dec(line, sizeof(line), &len, ram_mebibytes(paging_mapped_bytes()));
    append_text(line, sizeof(line), &len, "M");
    console_write_at(1, 3, 0x0f, line);

    build_bar(bar, sizeof(bar), g_sessions[g_active_session].focus_ticks, g_ticks ? g_ticks : 1, 16);
    len = 0;
    line[0] = '\0';
    append_text(line, sizeof(line), &len, "cpu  ");
    append_text(line, sizeof(line), &len, bar);
    append_char(line, sizeof(line), &len, ' ');
    append_u64_dec(line, sizeof(line), &len, ticks_to_seconds(g_sessions[g_active_session].focus_ticks));
    append_text(line, sizeof(line), &len, "s");
    console_write_at(1, 4, 0x0f, line);

    console_fill_row(row, 0x08);
    console_write_at(1, row++, 0x0f, "pid usr kd  st      tk  in cm dr");
    shell_render_task_row(row++, 0, "sys", "krn", "run", g_ticks, 0, 0, 0, 0x0f);

    for (int i = 0; i < SHELL_SESSION_COUNT; ++i) {
        shell_render_task_row(row++,
                              (u32)(i + 1),
                              g_sessions[i].user,
                              "sh",
                              session_state_label(i),
                              g_sessions[i].focus_ticks,
                              g_sessions[i].key_count,
                              g_sessions[i].command_count,
                              g_sessions[i].render_count,
                              i == g_active_session ? 0x0e : 0x0f);
    }

    if (snake_active() && g_snake_owner_session >= 0 && g_snake_owner_session < SHELL_SESSION_COUNT && row < console_height() - 1) {
        shell_render_task_row(row,
                              (u32)(40 + g_snake_owner_session + 1),
                              g_sessions[g_snake_owner_session].user,
                              "snk",
                              g_active_session == g_snake_owner_session && g_shell_view == SHELL_VIEW_SESSION ? "run" : "hold",
                              g_snake_ticks,
                              g_snake_input_count,
                              0,
                              g_snake_render_count,
                              0x0a);
    }

    g_shell_dirty = 0;
}

static void shell_render_installer(void) {
    char line[96];
    usize len = 0;

    console_clear(0x01);
    console_fill_row(0, 0x1f);
    console_fill_row(1, 0x87);
    console_write_at(1, 0, 0x1f, "flareOS / installer");
    console_write_at(1, 1, 0x87, "1 qwerty  2 azerty  enter install  q back");

    line[0] = '\0';
    append_text(line, sizeof(line), &len, "keyboard ");
    append_text(line, sizeof(line), &len, g_installer_layout == KEYBOARD_LAYOUT_AZERTY ? "azerty" : "qwerty");
    console_write_at(2, 4, 0x0f, line);

    len = 0;
    line[0] = '\0';
    append_text(line, sizeof(line), &len, "disk ");
    append_text(line, sizeof(line), &len, ata_present() ? "ata primary master online" : "no ata disk");
    console_write_at(2, 6, 0x0f, line);

    len = 0;
    line[0] = '\0';
    append_text(line, sizeof(line), &len, "source ");
    append_text(line, sizeof(line), &len, fs_backend_name());
    append_text(line, sizeof(line), &len, " files=");
    append_u64_dec(line, sizeof(line), &len, fs_file_count());
    console_write_at(2, 8, 0x0f, line);

    len = 0;
    line[0] = '\0';
    append_text(line, sizeof(line), &len, "target flarefs rw on disk region ");
    append_u64_dec(line, sizeof(line), &len, 321);
    console_write_at(2, 10, 0x0f, line);

    console_write_at(2, 13, 0x0f, "installer writes the current root into flarefs");
    console_write_at(2, 15, 0x0f, "serial logs stay on com1 while graphics use framebuffer");
    g_shell_dirty = 0;
}

static void shell_consume_snake_result(void) {
    u32 score = 0;

    if (snake_consume_result(&score) && g_snake_owner_session >= 0 && g_snake_owner_session < SHELL_SESSION_COUNT) {
        ShellSession *session = &g_sessions[g_snake_owner_session];
        char line[64];
        usize len = 0;

        line[0] = '\0';
        append_text(line, sizeof(line), &len, "snake score ");
        append_u64_dec(line, sizeof(line), &len, score);
        session_push_line(session, line);
        g_snake_owner_session = -1;
        g_shell_dirty = 1;
    }
}

static void shell_run_command(ShellSession *session) {
    char line[SHELL_INPUT_LEN];

    strcpy(line, session->input);
    if (line[0]) {
        serial_write("shell: ");
        serial_write(line);
        serial_write("\n");
        session_push_line(session, line);
        ++session->command_count;
    }

    if (strcmp(line, "help") == 0) {
        session_push_line(session, "help clear echo ls cat pwd uname whoami");
        session_push_line(session, "users uptime mem paging snake sessions");
        session_push_line(session, "switch <tty|user> top layout install");
        session_push_line(session, "write append rm panic halt net curl <url>");
    } else if (strcmp(line, "clear") == 0) {
        session->line_count = 0;
    } else if (strncmp(line, "echo ", 5) == 0) {
        session_push_line(session, line + 5);
    } else if (strcmp(line, "ls") == 0) {
        shell_list_files(session);
    } else if (strncmp(line, "write ", 6) == 0) {
        shell_write_file_command(session, line + 6, 0);
    } else if (strncmp(line, "append ", 7) == 0) {
        shell_write_file_command(session, line + 7, 1);
    } else if (strncmp(line, "rm ", 3) == 0) {
        shell_remove_file_command(session, line + 3);
    } else if (strncmp(line, "cat ", 4) == 0) {
        shell_cat_file(session, line + 4);
    } else if (strcmp(line, "pwd") == 0) {
        session_push_line(session, "/");
    } else if (strcmp(line, "uname") == 0) {
        shell_print_uname(session);
    } else if (strcmp(line, "whoami") == 0) {
        session_push_line(session, session->user);
    } else if (strcmp(line, "users") == 0) {
        shell_list_users(session);
    } else if (strcmp(line, "root") == 0) {
        shell_report_root(session);
    } else if (strcmp(line, "uptime") == 0) {
        shell_print_uptime(session);
    } else if (strcmp(line, "mem") == 0) {
        shell_print_memory(session);
    } else if (strcmp(line, "paging") == 0) {
        shell_print_paging(session);
    } else if (strcmp(line, "layout") == 0) {
        shell_report_layout(session);
    } else if (strncmp(line, "layout ", 7) == 0) {
        shell_set_layout_command(session, skip_spaces(line + 7));
    } else if (strcmp(line, "install") == 0) {
        shell_open_installer();
    } else if (strcmp(line, "snake") == 0) {
        shell_launch_snake(session);
    } else if (strcmp(line, "sessions") == 0 || strcmp(line, "switch") == 0) {
        shell_open_switcher();
    } else if (strncmp(line, "switch ", 7) == 0) {
        int target = session_index_from_target(line + 7);
        if (target < 0) {
            session_push_line(session, "session not found");
        } else {
            shell_set_active_session(target);
        }
    } else if (strcmp(line, "top") == 0 || strcmp(line, "tasks") == 0 || strcmp(line, "taskman") == 0) {
        shell_open_task_manager();
    } else if (strcmp(line, "net") == 0) {
        shell_net_status(session);
    } else if (strncmp(line, "curl ", 5) == 0) {
        shell_curl(session, line + 5);
    } else if (strcmp(line, "panic") == 0 || strcmp(line, "crash") == 0) {
        shell_trigger_panic();
    } else if (strcmp(line, "halt") == 0) {
        shell_halt(session);
    } else if (line[0] != '\0') {
        session_push_line(session, "unknown command");
    }

    session->input_len = 0;
    session->input[0] = '\0';
}

static void shell_handle_switcher_key(u16 key) {
    if (key == KEY_ESC || key == 'q') {
        g_shell_view = SHELL_VIEW_SESSION;
    } else if (key == 'w' || key == 'k') {
        g_selected_session = g_selected_session > 0 ? g_selected_session - 1 : SHELL_SESSION_COUNT - 1;
    } else if (key == 's' || key == 'j') {
        g_selected_session = (g_selected_session + 1) % SHELL_SESSION_COUNT;
    } else if (key >= '1' && key < '1' + SHELL_SESSION_COUNT) {
        shell_set_active_session((int)(key - '1'));
        return;
    } else if (key == KEY_ENTER || key == ' ') {
        shell_set_active_session(g_selected_session);
        return;
    }
    g_shell_dirty = 1;
}

static void shell_handle_taskman_key(u16 key) {
    if (key == KEY_ESC || key == 'q') {
        g_shell_view = SHELL_VIEW_SESSION;
    } else if (key >= '1' && key < '1' + SHELL_SESSION_COUNT) {
        shell_set_active_session((int)(key - '1'));
        return;
    }
    g_shell_dirty = 1;
}

static void shell_handle_installer_key(u16 key) {
    ShellSession *session = active_session();

    if (key == KEY_ESC || key == 'q') {
        g_shell_view = SHELL_VIEW_SESSION;
        g_shell_dirty = 1;
        return;
    }
    if (key == '1') {
        g_installer_layout = KEYBOARD_LAYOUT_QWERTY;
        g_shell_dirty = 1;
        return;
    }
    if (key == '2') {
        g_installer_layout = KEYBOARD_LAYOUT_AZERTY;
        g_shell_dirty = 1;
        return;
    }
    if (key == KEY_ENTER) {
        const char *layout_name = g_installer_layout == KEYBOARD_LAYOUT_AZERTY ? "azerty" : "qwerty";

        keyboard_set_layout(g_installer_layout);
        if (fs_install()) {
            if (!fs_write_file("keyboard.cfg", layout_name, strlen(layout_name), 0)) {
                session_push_line(session, "installer wrote flarefs but layout save failed");
            } else {
                session_push_line(session, "installer wrote flarefs to disk");
            }
            shell_report_layout(session);
            shell_report_root(session);
        } else {
            session_push_line(session, "installer failed");
        }
        g_shell_view = SHELL_VIEW_SESSION;
        g_shell_dirty = 1;
    }
}

void shell_init(void) {
    static const char *users[SHELL_SESSION_COUNT] = { "root", "dev", "guest", "ops" };

    memset(g_sessions, 0, sizeof(g_sessions));
    g_active_session = 0;
    g_selected_session = 0;
    g_shell_view = SHELL_VIEW_SESSION;
    g_installer_layout = keyboard_layout();
    g_snake_owner_session = -1;
    g_snake_ticks = 0;
    g_snake_input_count = 0;
    g_snake_render_count = 0;

    for (int i = 0; i < SHELL_SESSION_COUNT; ++i) {
        ShellSession *session = &g_sessions[i];
        char line[48];
        usize len = 0;

        strcpy(session->user, users[i]);
        session->started_tick = g_ticks;
        session->last_input_tick = g_ticks;
        session->last_render_tick = g_ticks;

        line[0] = '\0';
        append_text(line, sizeof(line), &len, "flareOS tty");
        append_u64_dec(line, sizeof(line), &len, (u64)(i + 1));
        append_text(line, sizeof(line), &len, " / ");
        append_text(line, sizeof(line), &len, session->user);
        session_push_line(session, line);
        session_push_line(session, "framebuffer shell online");
        shell_report_root(session);
        shell_report_layout(session);
        session_push_line(session, "F1-F4 switch  F10 taskman");
        session_push_line(session, "type help, install, sessions or top");
    }

    ++g_sessions[0].switch_count;
    g_shell_dirty = 1;
}

static void shell_update_prompt(void) {
    ShellSession *session = active_session();
    char prompt[SHELL_INPUT_LEN + 20];
    int bottom = console_height() - 1;

    session_prompt_line(session, g_active_session, prompt, sizeof(prompt));
    console_fill_row(bottom, 0x01);
    console_write_at(0, bottom, 0x0f, prompt);
    console_set_cursor((int)strlen(prompt), bottom);
    ++session->render_count;
    g_prompt_dirty = 0;
}

void shell_render(void) {
    if (g_prompt_dirty && !g_shell_dirty) {
        shell_update_prompt();
        return;
    }
    g_prompt_dirty = 0;
    if (g_shell_view == SHELL_VIEW_SWITCHER) {
        shell_render_switcher();
        return;
    }
    if (g_shell_view == SHELL_VIEW_TASKMAN) {
        shell_render_task_manager();
        return;
    }
    if (g_shell_view == SHELL_VIEW_INSTALLER) {
        shell_render_installer();
        return;
    }
    shell_render_session_view();
}

void shell_keyboard_event(u16 key, int pressed) {
    ShellSession *session = active_session();

    if (!pressed) {
        return;
    }

    ++session->key_count;
    session->last_input_tick = g_ticks;

    if (key >= KEY_F1 && key <= KEY_F4) {
        shell_set_active_session((int)(key - KEY_F1));
        return;
    }
    if (key == KEY_F10) {
        shell_open_task_manager();
        return;
    }

    if (g_shell_view == SHELL_VIEW_SWITCHER) {
        shell_handle_switcher_key(key);
        return;
    }
    if (g_shell_view == SHELL_VIEW_TASKMAN) {
        shell_handle_taskman_key(key);
        return;
    }
    if (g_shell_view == SHELL_VIEW_INSTALLER) {
        shell_handle_installer_key(key);
        return;
    }

    if (snake_visible_for_active_session()) {
        ++g_snake_input_count;
        snake_keyboard_event(key, pressed);
        shell_consume_snake_result();
        g_shell_dirty = 1;
        return;
    }

    if (key == KEY_ESC) {
        shell_open_switcher();
        return;
    }
    if (key == KEY_ENTER) {
        shell_run_command(session);
        g_shell_dirty = 1;
    } else if (key == KEY_BACKSPACE) {
        if (session->input_len > 0) {
            session->input[--session->input_len] = '\0';
        }
        g_prompt_dirty = 1;
    } else if (key >= ' ' && key <= '~' && session->input_len + 1 < (int)sizeof(session->input)) {
        session->input[session->input_len++] = (char)key;
        session->input[session->input_len] = '\0';
        g_prompt_dirty = 1;
    }
}

void shell_timer_tick(void) {
    ++g_sessions[g_active_session].focus_ticks;

    if (snake_visible_for_active_session()) {
        ++g_snake_ticks;
        snake_timer_tick();
    }

    shell_consume_snake_result();
    if (g_shell_view == SHELL_VIEW_TASKMAN && (g_ticks % timer_hz()) == 0) {
        g_shell_dirty = 1;
    }
}

int shell_needs_redraw(void) {
    if (snake_visible_for_active_session()) {
        return g_shell_dirty || g_prompt_dirty || snake_needs_redraw();
    }
    return g_shell_dirty || g_prompt_dirty;
}
