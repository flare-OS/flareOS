#ifndef FLARE_BOOT_H
#define FLARE_BOOT_H

#include "common.h"

#define BOOTINFO_MAGIC 0x464C4152u

typedef struct {
    u32 magic;
    u32 boot_drive;
    u32 framebuffer_width;
    u32 framebuffer_height;
    u32 framebuffer_pitch;
    u32 framebuffer_bpp;
    u64 framebuffer_addr;
    u64 fs_addr;
    u32 fs_size;
    u32 timer_hz;
    u64 total_memory;
    u64 font_addr;
    u32 font_height;
    u32 reserved0;
} BootInfo;

#endif
