#include "kernel.h"

static u8 *heap_base = (u8 *)0x180000;
static usize heap_offset = 0;

void memory_init(void) {
    heap_offset = 0;
}

void *kmalloc(usize size) {
    size = (size + 15u) & ~15u;
    void *ptr = heap_base + heap_offset;
    heap_offset += size;
    return ptr;
}

usize heap_bytes_used(void) {
    return heap_offset;
}
