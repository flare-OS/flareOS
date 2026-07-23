#include "kernel.h"
#include "x86_64.h"

static const u16 qwerty_keymap[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0a] = '9', [0x0b] = '0', [0x0c] = '-', [0x0d] = '=',
    [0x0e] = KEY_BACKSPACE, [0x0f] = KEY_TAB,
    [0x01] = KEY_ESC,
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1a] = '[', [0x1b] = ']',
    [0x1c] = KEY_ENTER, [0x1e] = 'a', [0x1f] = 's', [0x20] = 'd',
    [0x21] = 'f', [0x22] = 'g', [0x23] = 'h', [0x24] = 'j',
    [0x25] = 'k', [0x26] = 'l', [0x27] = ';', [0x28] = '\'',
    [0x29] = '`', [0x2b] = '\\', [0x2c] = 'z', [0x2d] = 'x',
    [0x2e] = 'c', [0x2f] = 'v', [0x30] = 'b', [0x31] = 'n',
    [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x39] = ' ',
    [0x3b] = KEY_F1, [0x3c] = KEY_F2, [0x3d] = KEY_F3, [0x3e] = KEY_F4,
    [0x44] = KEY_F10,
};

static const u16 azerty_keymap[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0a] = '9', [0x0b] = '0', [0x0c] = '-', [0x0d] = '=',
    [0x0e] = KEY_BACKSPACE, [0x0f] = KEY_TAB,
    [0x01] = KEY_ESC,
    [0x10] = 'a', [0x11] = 'z', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1a] = '[', [0x1b] = ']',
    [0x1c] = KEY_ENTER, [0x1e] = 'q', [0x1f] = 's', [0x20] = 'd',
    [0x21] = 'f', [0x22] = 'g', [0x23] = 'h', [0x24] = 'j',
    [0x25] = 'k', [0x26] = 'l', [0x27] = 'm', [0x28] = '\'',
    [0x29] = '`', [0x2b] = '\\', [0x2c] = 'w', [0x2d] = 'x',
    [0x2e] = 'c', [0x2f] = 'v', [0x30] = 'b', [0x31] = 'n',
    [0x32] = ',', [0x33] = ';', [0x34] = '.', [0x35] = '/',
    [0x39] = ' ',
    [0x3b] = KEY_F1, [0x3c] = KEY_F2, [0x3d] = KEY_F3, [0x3e] = KEY_F4,
    [0x44] = KEY_F10,
};

#define KEY_QUEUE_SIZE 32

static volatile u16 key_queue[KEY_QUEUE_SIZE];
static volatile u8 key_queue_head = 0;
static volatile u8 key_queue_tail = 0;
static const u16 *g_keymap = qwerty_keymap;
static int g_layout = KEYBOARD_LAYOUT_QWERTY;
static int g_shift = 0;
static int g_caps = 0;
static int g_extended = 0;
static int g_ctrl = 0;

static const u16 g_ext_keymap[128] = {
    [0x47] = KEY_HOME, [0x48] = KEY_UP, [0x49] = KEY_PGUP,
    [0x4B] = KEY_LEFT, [0x4D] = KEY_RIGHT,
    [0x4F] = KEY_END,  [0x50] = KEY_DOWN, [0x51] = KEY_PGDN,
    [0x52] = KEY_INSERT, [0x53] = KEY_DEL,
};

static u16 shift_value(u16 key) {
    switch (key) {
        case '1': return '!'; case '2': return '@'; case '3': return '#';
        case '4': return '$'; case '5': return '%'; case '6': return '^';
        case '7': return '&'; case '8': return '*'; case '9': return '(';
        case '0': return ')'; case '-': return '_'; case '=': return '+';
        case '[': return '{'; case ']': return '}';
        case ';': return ':'; case '\'': return '"';
        case '`': return '~'; case '\\': return '|';
        case ',': return '<'; case '.': return '>'; case '/': return '?';
        default: return 0;
    }
}

void keyboard_handle_irq(void) {
    u8 scancode = inb(0x60);

    if (scancode == 0xE0) { g_extended = 1; return; }
    if (scancode == 0x1D) { g_ctrl = 1; return; }
    if (scancode == 0x9D) { g_ctrl = 0; return; }
    if (scancode == 0x2A || scancode == 0x36) { g_shift = 1; return; }
    if (scancode == 0xAA || scancode == 0xB6) { g_shift = 0; return; }
    if (scancode == 0x3A) { g_caps = !g_caps; return; }
    if (scancode & 0x80u) { g_extended = 0; return; }

    u16 key;
    if (g_extended) {
        g_extended = 0;
        if (scancode >= 128) return;
        key = g_ext_keymap[scancode];
        if (key == KEY_NONE) return;
    } else {
        if (scancode >= 128) return;
        key = g_keymap[scancode];
        if (key == KEY_NONE) return;
        if (g_ctrl && key >= 'a' && key <= 'z') {
            key = 0x200u | key;
        } else if (key >= 'a' && key <= 'z') {
            if (g_shift ^ g_caps) key = (u16)(key - 'a' + 'A');
        } else if (g_shift) {
            u16 s = shift_value(key);
            if (s) key = s;
        }
    }

    u8 next_head = (u8)((key_queue_head + 1u) % KEY_QUEUE_SIZE);
    if (next_head == key_queue_tail) return;
    key_queue[key_queue_head] = key;
    key_queue_head = next_head;
}

void keyboard_poll_event(void) {
    if (key_queue_tail != key_queue_head) {
        u16 key = key_queue[key_queue_tail];
        key_queue_tail = (u8)((key_queue_tail + 1u) % KEY_QUEUE_SIZE);
        shell_keyboard_event(key, 1);
    }
}

void keyboard_set_layout(int layout) {
    if (layout == KEYBOARD_LAYOUT_AZERTY) {
        g_keymap = azerty_keymap;
        g_layout = KEYBOARD_LAYOUT_AZERTY;
        return;
    }

    g_keymap = qwerty_keymap;
    g_layout = KEYBOARD_LAYOUT_QWERTY;
}

int keyboard_layout(void) {
    return g_layout;
}

const char *keyboard_layout_name(void) {
    return g_layout == KEYBOARD_LAYOUT_AZERTY ? "azerty" : "qwerty";
}
