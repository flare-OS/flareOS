#include "kernel.h"
#include "x86_64.h"

#define PAGE_SIZE 0x1000ull
#define PAGE_PRESENT 0x001ull
#define PAGE_WRITABLE 0x002ull
#define PAGE_POOL_COUNT 64
#define LOW_IDENTITY_LIMIT 0x04000000ull

static u64 page_pool[PAGE_POOL_COUNT][512] __attribute__((aligned(4096)));
static usize page_pool_used = 0;
static u64 *pml4;
static u64 paging_pages_mapped = 0;

static u64 *alloc_table(void) {
    u64 *table;

    if (page_pool_used >= PAGE_POOL_COUNT) {
        panic("out of paging tables");
    }

    table = page_pool[page_pool_used++];
    memset(table, 0, PAGE_SIZE);
    return table;
}

static u64 *table_from_entry(u64 entry) {
    return (u64 *)(usize)(entry & ~0xfffull);
}

static u64 *paging_walk_create(u64 *table, usize index) {
    if ((table[index] & PAGE_PRESENT) == 0) {
        u64 *next = alloc_table();
        table[index] = (u64)(usize)next | PAGE_PRESENT | PAGE_WRITABLE;
    }
    return table_from_entry(table[index]);
}

static void paging_map_page(u64 virt, u64 phys, u64 flags) {
    u64 *pdpt = paging_walk_create(pml4, (usize)((virt >> 39) & 0x1ff));
    u64 *pd = paging_walk_create(pdpt, (usize)((virt >> 30) & 0x1ff));
    u64 *pt = paging_walk_create(pd, (usize)((virt >> 21) & 0x1ff));
    usize pt_index = (usize)((virt >> 12) & 0x1ff);

    pt[pt_index] = (phys & ~0xfffull) | (flags & 0xfffull) | PAGE_PRESENT;
    ++paging_pages_mapped;
}

static void paging_map_identity_range(u64 base, u64 size, u64 flags) {
    u64 start = base & ~(PAGE_SIZE - 1ull);
    u64 end = (base + size + PAGE_SIZE - 1ull) & ~(PAGE_SIZE - 1ull);

    for (u64 addr = start; addr < end; addr += PAGE_SIZE) {
        paging_map_page(addr, addr, flags);
    }
}

void paging_init(void) {
    u64 framebuffer_base = g_boot_info.framebuffer_addr & ~0xfffull;
    u64 framebuffer_size = (u64)g_boot_info.framebuffer_pitch * g_boot_info.framebuffer_height;

    page_pool_used = 0;
    paging_pages_mapped = 0;
    pml4 = alloc_table();
    paging_map_identity_range(0, LOW_IDENTITY_LIMIT, PAGE_WRITABLE);
    if (framebuffer_base >= LOW_IDENTITY_LIMIT && framebuffer_size != 0) {
        paging_map_identity_range(framebuffer_base, framebuffer_size, PAGE_WRITABLE);
    }
    write_cr3((u64)(usize)pml4);
}

u64 paging_mapped_bytes(void) {
    return paging_pages_mapped * PAGE_SIZE;
}

u64 paging_table_count(void) {
    return (u64)page_pool_used;
}
