#include "kernel.h"
#include "x86_64.h"

#define PAGE_SIZE       0x1000ull
#define PAGE_SIZE_2MB   0x200000ull
#define PAGE_PRESENT    0x001ull
#define PAGE_WRITABLE   0x002ull
#define PAGE_USER       0x004ull
#define PAGE_PS         0x080ull

#define PAGE_POOL_COUNT 256

#define MAX_IDENTITY    0x400000000ull

static u64 page_pool[PAGE_POOL_COUNT][512] __attribute__((aligned(4096)));
static usize page_pool_used = 0;
static u64 *g_pml4;
static u64 paging_pages_mapped = 0;

u64 current_cr3(void) {
    u64 val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}

static u64 *alloc_table(void) {
    if (page_pool_used >= PAGE_POOL_COUNT)
        panic("out of paging tables");
    u64 *table = page_pool[page_pool_used++];
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
    u64 *pdpt  = paging_walk_create(g_pml4, (usize)((virt >> 39) & 0x1ff));
    u64 *pd    = paging_walk_create(pdpt, (usize)((virt >> 30) & 0x1ff));
    u64 *pt    = paging_walk_create(pd,   (usize)((virt >> 21) & 0x1ff));
    usize pt_index = (usize)((virt >> 12) & 0x1ff);

    pt[pt_index] = (phys & ~0xfffull) | (flags & 0xfffull) | PAGE_PRESENT;
    ++paging_pages_mapped;
}

static void paging_map_page_2mb(u64 virt, u64 phys, u64 flags) {
    u64 *pdpt = paging_walk_create(g_pml4, (usize)((virt >> 39) & 0x1ff));
    u64 *pd   = paging_walk_create(pdpt, (usize)((virt >> 30) & 0x1ff));
    usize pd_index = (usize)((virt >> 21) & 0x1ff);

    pd[pd_index] = (phys & ~(PAGE_SIZE_2MB - 1ull))
                 | (flags & 0x1ffull)
                 | PAGE_PRESENT
                 | PAGE_PS;
    ++paging_pages_mapped;
}

static void paging_map_identity_range(u64 base, u64 size, u64 flags) {
    u64 start = base & ~(PAGE_SIZE_2MB - 1ull);
    u64 end   = (base + size + PAGE_SIZE_2MB - 1ull) & ~(PAGE_SIZE_2MB - 1ull);

    for (u64 addr = start; addr < end; addr += PAGE_SIZE_2MB)
        paging_map_page_2mb(addr, addr, flags);
}

void paging_map_range(u64 virt, u64 phys, u64 size, u64 flags) {
    u64 v = virt & ~(PAGE_SIZE_2MB - 1ull);
    u64 p = phys & ~(PAGE_SIZE_2MB - 1ull);
    u64 e = (virt + size + PAGE_SIZE_2MB - 1ull) & ~(PAGE_SIZE_2MB - 1ull);

    for (; v < e; v += PAGE_SIZE_2MB, p += PAGE_SIZE_2MB)
        paging_map_page_2mb(v, p, flags);
}

void paging_map_page_4k(u64 virt, u64 phys, u64 flags) {
    paging_map_page(virt, phys, flags);
}

u64 paging_alloc_pml4_copy(void) {
    u64 *pml4 = alloc_table();
    for (int i = 0; i < 512; ++i) {
        u64 entry = g_pml4[i];
        if (entry & PAGE_PRESENT)
            pml4[i] = entry;
    }
    return (u64)(usize)pml4;
}

void paging_activate(u64 cr3_value) {
    write_cr3(cr3_value);
}

void paging_init(void) {
    u64 framebuffer_base  = g_boot_info.framebuffer_addr & ~(PAGE_SIZE_2MB - 1ull);
    u64 framebuffer_size  = (u64)g_boot_info.framebuffer_pitch * g_boot_info.framebuffer_height;
    u64 total_mem         = g_boot_info.total_memory;

    page_pool_used = 0;
    paging_pages_mapped = 0;
    g_pml4 = alloc_table();

    u64 identity_size = 0x04000000ull;
    if (total_mem > 0 && total_mem <= MAX_IDENTITY)
        identity_size = total_mem;
    else if (total_mem > MAX_IDENTITY)
        identity_size = MAX_IDENTITY;

    paging_map_identity_range(0, identity_size, PAGE_WRITABLE);

    if (framebuffer_base >= identity_size && framebuffer_size != 0)
        paging_map_identity_range(framebuffer_base, framebuffer_size, PAGE_WRITABLE);

    write_cr3((u64)(usize)g_pml4);
}

u64 paging_mapped_bytes(void) {
    return paging_pages_mapped * PAGE_SIZE_2MB;
}

u64 paging_table_count(void) {
    return (u64)page_pool_used;
}
