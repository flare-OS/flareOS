#include "kernel.h"

#define EXT4_SUPER_OFFSET 1024u
#define EXT4_SUPER_MAGIC 0xef53u
#define EXT4_EXTENTS_FL 0x00080000u
#define EXT4_FEATURE_INCOMPAT_EXTENTS 0x00000040u
#define EXT4_S_IFDIR 0x4000u
#define EXT4_S_IFREG 0x8000u
#define EXT4_FT_REG_FILE 1u
#define EXT4_EXT_MAGIC 0xf30au
#define EXT4_ROOT_INO 2u

#define FLAREFS_MAGIC 0x31534646u
#define FLAREFS_VERSION 1u
#define FLAREFS_LBA 321u
#define FLAREFS_SECTORS 256u
#define FLAREFS_DIR_SECTORS 4u
#define FLAREFS_NAME_LEN 48
#define FS_MAX_ROOT_FILES 32

typedef struct {
    char name[FLAREFS_NAME_LEN];
    const char *data;
    usize size;
    u32 dir_index;
} FsFile;

struct __attribute__((packed)) Ext4ExtentHeader {
    u16 magic;
    u16 entries;
    u16 max;
    u16 depth;
    u32 generation;
};

struct __attribute__((packed)) Ext4Extent {
    u32 block;
    u16 len;
    u16 start_hi;
    u32 start_lo;
};

struct __attribute__((packed)) Ext4ExtentIndex {
    u32 block;
    u32 leaf_lo;
    u16 leaf_hi;
    u16 unused;
};

typedef struct __attribute__((packed)) {
    u32 magic;
    u32 version;
    u32 start_lba;
    u32 total_sectors;
    u32 dir_entries;
    u32 dir_sectors;
    u32 next_free_sector;
    u32 reserved;
} FlareFsSuper;

typedef struct __attribute__((packed)) {
    u32 used;
    u32 start_sector;
    u32 sector_count;
    u32 size;
    char name[FLAREFS_NAME_LEN];
} FlareFsDirEntry;

static const u8 *g_source_base;
static u32 g_source_size;
static u32 g_block_size;
static u32 g_inode_size;
static u32 g_inodes_per_group;
static u32 g_desc_size;
static const u8 *g_group_desc_table;

static FsFile g_source_files[FS_MAX_ROOT_FILES];
static usize g_source_file_count;
static FsFile g_files[FS_MAX_ROOT_FILES];
static usize g_file_count;
static FlareFsSuper g_super;
static FlareFsDirEntry g_dir[FS_MAX_ROOT_FILES];
static int g_fs_writable = 0;
static const char *g_backend_name = "install-media";

static void files_reset(FsFile *files, usize *count) {
    *count = 0;
    memset(files, 0, sizeof(FsFile) * FS_MAX_ROOT_FILES);
}

static void fs_reset_source(void) {
    g_source_base = NULL;
    g_source_size = 0;
    g_block_size = 0;
    g_inode_size = 0;
    g_inodes_per_group = 0;
    g_desc_size = 0;
    g_group_desc_table = NULL;
    files_reset(g_source_files, &g_source_file_count);
}

static void fs_reset_active(void) {
    files_reset(g_files, &g_file_count);
    memset(&g_super, 0, sizeof(g_super));
    memset(g_dir, 0, sizeof(g_dir));
    g_fs_writable = 0;
    g_backend_name = "install-media";
}

static u16 read_u16(const void *ptr) {
    return *(const u16 *)ptr;
}

static u32 read_u32(const void *ptr) {
    return *(const u32 *)ptr;
}

static const u8 *source_region(u64 offset, usize size) {
    if (!g_source_base || offset + (u64)size > g_source_size) {
        return NULL;
    }
    return g_source_base + offset;
}

static const u8 *source_block(u64 block, usize size) {
    return source_region(block * (u64)g_block_size, size);
}

static const u8 *ext4_inode(u32 inode_no) {
    const u8 *desc;
    u32 group;
    u32 index;
    u32 inode_table_block;
    u64 inode_offset;

    if (inode_no == 0 || g_inodes_per_group == 0 || !g_group_desc_table) {
        return NULL;
    }

    group = (inode_no - 1u) / g_inodes_per_group;
    index = (inode_no - 1u) % g_inodes_per_group;
    desc = source_region((u64)(g_group_desc_table - g_source_base) + (u64)group * g_desc_size, 12);
    if (!desc) {
        return NULL;
    }

    inode_table_block = read_u32(desc + 8);
    inode_offset = (u64)inode_table_block * g_block_size + (u64)index * g_inode_size;
    return source_region(inode_offset, g_inode_size);
}

static u16 inode_mode(const u8 *inode) {
    return read_u16(inode + 0);
}

static u32 inode_flags(const u8 *inode) {
    return read_u32(inode + 32);
}

static usize inode_size_bytes(const u8 *inode) {
    u64 size_lo = read_u32(inode + 4);
    u64 size_hi = read_u32(inode + 108);
    return (usize)(size_lo | (size_hi << 32));
}

static int ext4_copy_extent_tree(const struct Ext4ExtentHeader *header, u8 *dest, usize size) {
    if (!header || header->magic != EXT4_EXT_MAGIC) {
        return 0;
    }

    if (header->depth == 0) {
        const struct Ext4Extent *extent = (const struct Ext4Extent *)(header + 1);

        for (u16 i = 0; i < header->entries; ++i) {
            u16 block_count = extent[i].len & 0x7fff;
            u64 start_block = ((u64)extent[i].start_hi << 32) | extent[i].start_lo;
            usize dest_offset = (usize)extent[i].block * g_block_size;
            usize copy_bytes = (usize)block_count * g_block_size;
            const u8 *src;

            if ((extent[i].len & 0x8000u) != 0 || dest_offset >= size) {
                continue;
            }
            if (dest_offset + copy_bytes > size) {
                copy_bytes = size - dest_offset;
            }
            src = source_block(start_block, copy_bytes);
            if (!src) {
                return 0;
            }
            memcpy(dest + dest_offset, src, copy_bytes);
        }
        return 1;
    }

    {
        const struct Ext4ExtentIndex *index = (const struct Ext4ExtentIndex *)(header + 1);

        for (u16 i = 0; i < header->entries; ++i) {
            u64 leaf_block = ((u64)index[i].leaf_hi << 32) | index[i].leaf_lo;
            const struct Ext4ExtentHeader *child = (const struct Ext4ExtentHeader *)source_block(leaf_block, g_block_size);

            if (!child || !ext4_copy_extent_tree(child, dest, size)) {
                return 0;
            }
        }
    }

    return 1;
}

static int ext4_copy_direct_blocks(const u8 *inode, u8 *dest, usize size) {
    for (u32 i = 0; i < 12; ++i) {
        u32 block = read_u32(inode + 40 + i * 4);
        usize dest_offset = (usize)i * g_block_size;
        usize copy_bytes = g_block_size;
        const u8 *src;

        if (block == 0 || dest_offset >= size) {
            continue;
        }
        if (dest_offset + copy_bytes > size) {
            copy_bytes = size - dest_offset;
        }
        src = source_block(block, copy_bytes);
        if (!src) {
            return 0;
        }
        memcpy(dest + dest_offset, src, copy_bytes);
    }
    return 1;
}

static int ext4_load_inode_data(const u8 *inode, u8 *dest, usize size) {
    memset(dest, 0, size);
    if (size == 0) {
        return 1;
    }
    if (inode_flags(inode) & EXT4_EXTENTS_FL) {
        return ext4_copy_extent_tree((const struct Ext4ExtentHeader *)(inode + 40), dest, size);
    }
    return ext4_copy_direct_blocks(inode, dest, size);
}

static void ext4_register_file(const char *name, usize name_len, const u8 *inode) {
    FsFile *file;
    usize size;
    char *data;

    if (!inode || g_source_file_count >= FS_MAX_ROOT_FILES || name_len >= sizeof(g_source_files[0].name)) {
        return;
    }
    if ((inode_mode(inode) & EXT4_S_IFREG) != EXT4_S_IFREG) {
        return;
    }

    file = &g_source_files[g_source_file_count];
    size = inode_size_bytes(inode);
    data = (char *)kmalloc(size == 0 ? 1 : size);
    if (!data) {
        return;
    }
    if (!ext4_load_inode_data(inode, (u8 *)data, size)) {
        return;
    }

    memcpy(file->name, name, name_len);
    file->name[name_len] = '\0';
    file->data = data;
    file->size = size;
    file->dir_index = 0xffffffffu;
    ++g_source_file_count;
}

static void ext4_scan_root(void) {
    const u8 *root_inode = ext4_inode(EXT4_ROOT_INO);
    usize root_size;
    u8 *dir_data;
    usize offset = 0;

    if (!root_inode || (inode_mode(root_inode) & EXT4_S_IFDIR) != EXT4_S_IFDIR) {
        return;
    }

    root_size = inode_size_bytes(root_inode);
    dir_data = (u8 *)kmalloc(root_size == 0 ? 1 : root_size);
    if (!dir_data || !ext4_load_inode_data(root_inode, dir_data, root_size)) {
        return;
    }

    while (offset + 8 <= root_size) {
        const u8 *entry = dir_data + offset;
        u32 inode_no = read_u32(entry + 0);
        u16 rec_len = read_u16(entry + 4);
        u8 name_len = entry[6];
        u8 file_type = entry[7];

        if (rec_len < 8 || offset + rec_len > root_size) {
            break;
        }

        if (inode_no != 0 && name_len > 0) {
            const char *name = (const char *)(entry + 8);
            const u8 *inode = ext4_inode(inode_no);

            if (inode && (file_type == EXT4_FT_REG_FILE || (inode_mode(inode) & EXT4_S_IFREG) == EXT4_S_IFREG)) {
                ext4_register_file(name, name_len, inode);
            }
        }
        offset += rec_len;
    }
}

static void ext4_init_source(const void *base, u32 size) {
    const u8 *super;
    u32 feature_incompat;
    u32 bgdt_offset;

    fs_reset_source();
    g_source_base = (const u8 *)base;
    g_source_size = size;
    super = source_region(EXT4_SUPER_OFFSET, 1024);
    if (!super || read_u16(super + 56) != EXT4_SUPER_MAGIC) {
        fs_reset_source();
        return;
    }

    g_block_size = 1024u << read_u32(super + 24);
    g_inodes_per_group = read_u32(super + 40);
    g_inode_size = read_u16(super + 88);
    g_desc_size = read_u16(super + 254);
    feature_incompat = read_u32(super + 96);
    if (g_inode_size == 0) {
        g_inode_size = 128;
    }
    if (g_desc_size < 32) {
        g_desc_size = 32;
    }
    if ((feature_incompat & EXT4_FEATURE_INCOMPAT_EXTENTS) == 0) {
        fs_reset_source();
        return;
    }

    bgdt_offset = (g_block_size == 1024u) ? 2048u : g_block_size;
    g_group_desc_table = source_region(bgdt_offset, g_desc_size);
    if (!g_group_desc_table) {
        fs_reset_source();
        return;
    }

    ext4_scan_root();
}

static int flarefs_read_sector(u32 sector, void *buffer) {
    return ata_read_sector(FLAREFS_LBA + sector, buffer);
}

static int flarefs_write_sector(u32 sector, const void *buffer) {
    return ata_write_sector(FLAREFS_LBA + sector, buffer);
}

static int flarefs_write_super(void) {
    u8 sector[512];

    memset(sector, 0, sizeof(sector));
    memcpy(sector, &g_super, sizeof(g_super));
    return flarefs_write_sector(0, sector);
}

static int flarefs_write_directory(void) {
    const u8 *src = (const u8 *)g_dir;

    for (u32 sector = 0; sector < FLAREFS_DIR_SECTORS; ++sector) {
        if (!flarefs_write_sector(1u + sector, src + sector * 512u)) {
            return 0;
        }
    }
    return 1;
}

static int flarefs_valid_name(const char *name) {
    usize len = 0;

    if (!name || name[0] == '\0') {
        return 0;
    }
    while (name[len]) {
        char ch = name[len];

        if (len + 1 >= FLAREFS_NAME_LEN || ch <= ' ' || ch == '/' || ch == '\\') {
            return 0;
        }
        ++len;
    }
    return 1;
}

static int flarefs_find_slot(const char *name) {
    for (usize i = 0; i < g_file_count; ++i) {
        if (strcmp(g_files[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void active_set_backend(const char *name, int writable) {
    g_backend_name = name;
    g_fs_writable = writable;
}

static void active_clone_source(void) {
    files_reset(g_files, &g_file_count);
    for (usize i = 0; i < g_source_file_count; ++i) {
        g_files[g_file_count++] = g_source_files[i];
    }
    active_set_backend("install-media", 0);
}

static int flarefs_mount(void) {
    u8 sector[512];

    if (!ata_present()) {
        return 0;
    }
    if (!flarefs_read_sector(0, sector)) {
        return 0;
    }
    memcpy(&g_super, sector, sizeof(g_super));
    if (g_super.magic != FLAREFS_MAGIC || g_super.version != FLAREFS_VERSION) {
        return 0;
    }
    if (g_super.start_lba != FLAREFS_LBA || g_super.total_sectors != FLAREFS_SECTORS || g_super.dir_entries > FS_MAX_ROOT_FILES || g_super.dir_sectors != FLAREFS_DIR_SECTORS) {
        return 0;
    }

    {
        u8 *dst = (u8 *)g_dir;

        memset(g_dir, 0, sizeof(g_dir));
        for (u32 dir_sector = 0; dir_sector < FLAREFS_DIR_SECTORS; ++dir_sector) {
            if (!flarefs_read_sector(1u + dir_sector, dst + dir_sector * 512u)) {
                return 0;
            }
        }
    }

    files_reset(g_files, &g_file_count);
    for (u32 i = 0; i < g_super.dir_entries; ++i) {
        FlareFsDirEntry *entry = &g_dir[i];
        u8 *data;
        u8 sector_data[512];

        if (!entry->used || entry->name[0] == '\0' || g_file_count >= FS_MAX_ROOT_FILES) {
            continue;
        }

        data = (u8 *)kmalloc(entry->size == 0 ? 1 : entry->size);
        if (!data) {
            return 0;
        }

        for (u32 sector_index = 0; sector_index < entry->sector_count; ++sector_index) {
            usize offset = (usize)sector_index * 512u;
            usize copy = 512u;

            if (!flarefs_read_sector(entry->start_sector + sector_index, sector_data)) {
                return 0;
            }
            if (offset >= entry->size) {
                break;
            }
            if (offset + copy > entry->size) {
                copy = entry->size - offset;
            }
            memcpy(data + offset, sector_data, copy);
        }

        strcpy(g_files[g_file_count].name, entry->name);
        g_files[g_file_count].data = (const char *)data;
        g_files[g_file_count].size = entry->size;
        g_files[g_file_count].dir_index = i;
        ++g_file_count;
    }

    active_set_backend("flarefs", 1);
    serial_write("flarefs: mounted writable root\n");
    return 1;
}

static int flarefs_format(void) {
    u8 zero[512];

    if (!ata_present()) {
        serial_write("flarefs: format aborted, no ata disk\n");
        return 0;
    }

    memset(zero, 0, sizeof(zero));
    for (u32 sector = 0; sector < FLAREFS_SECTORS; ++sector) {
        if (!flarefs_write_sector(sector, zero)) {
            return 0;
        }
    }

    memset(&g_super, 0, sizeof(g_super));
    g_super.magic = FLAREFS_MAGIC;
    g_super.version = FLAREFS_VERSION;
    g_super.start_lba = FLAREFS_LBA;
    g_super.total_sectors = FLAREFS_SECTORS;
    g_super.dir_entries = FS_MAX_ROOT_FILES;
    g_super.dir_sectors = FLAREFS_DIR_SECTORS;
    g_super.next_free_sector = 1u + FLAREFS_DIR_SECTORS;
    memset(g_dir, 0, sizeof(g_dir));

    if (!flarefs_write_super() || !flarefs_write_directory()) {
        serial_write("flarefs: format write failure\n");
        return 0;
    }

    files_reset(g_files, &g_file_count);
    active_set_backend("flarefs", 1);
    serial_write("flarefs: formatted disk region\n");
    return 1;
}

static int flarefs_write_internal(const char *name, const char *data, usize size, int append) {
    int slot;
    int file_index;
    u32 dir_index;
    usize final_size = size;
    const char *final_data = data;
    char *combined = NULL;
    char *stored_data;
    u32 sector_count;
    u32 start_sector;
    FlareFsDirEntry *entry;
    u8 sector_data[512];

    if (!g_fs_writable || !flarefs_valid_name(name) || !data) {
        serial_write("flarefs: write rejected\n");
        return 0;
    }

    slot = flarefs_find_slot(name);
    if (append && slot >= 0) {
        combined = (char *)kmalloc(g_files[slot].size + size);
        if (!combined) {
            return 0;
        }
        memcpy(combined, g_files[slot].data, g_files[slot].size);
        memcpy(combined + g_files[slot].size, data, size);
        final_data = combined;
        final_size = g_files[slot].size + size;
    }

    if (slot >= 0) {
        dir_index = g_files[slot].dir_index;
        file_index = slot;
    } else {
        dir_index = 0xffffffffu;
        for (u32 i = 0; i < g_super.dir_entries; ++i) {
            if (!g_dir[i].used) {
                dir_index = i;
                break;
            }
        }
        if (dir_index == 0xffffffffu || g_file_count >= FS_MAX_ROOT_FILES) {
            return 0;
        }
        file_index = (int)g_file_count;
    }

    sector_count = (u32)((final_size + 511u) / 512u);
    if (sector_count == 0) {
        sector_count = 1;
    }
    start_sector = g_super.next_free_sector;
    if (start_sector + sector_count > g_super.total_sectors) {
        serial_write("flarefs: no space left\n");
        return 0;
    }

    for (u32 sector = 0; sector < sector_count; ++sector) {
        usize offset = (usize)sector * 512u;
        usize copy = 512u;

        memset(sector_data, 0, sizeof(sector_data));
        if (offset < final_size) {
            if (offset + copy > final_size) {
                copy = final_size - offset;
            }
            memcpy(sector_data, final_data + offset, copy);
        }
        if (!flarefs_write_sector(start_sector + sector, sector_data)) {
            return 0;
        }
    }

    entry = &g_dir[dir_index];
    memset(entry, 0, sizeof(*entry));
    entry->used = 1;
    entry->start_sector = start_sector;
    entry->sector_count = sector_count;
    entry->size = (u32)final_size;
    strcpy(entry->name, name);
    g_super.next_free_sector = start_sector + sector_count;

    if (!flarefs_write_directory() || !flarefs_write_super()) {
        serial_write("flarefs: metadata update failed\n");
        return 0;
    }

    stored_data = (char *)kmalloc(final_size == 0 ? 1 : final_size);
    if (!stored_data) {
        return 0;
    }
    if (final_size != 0) {
        memcpy(stored_data, final_data, final_size);
    }

    strcpy(g_files[file_index].name, name);
    g_files[file_index].data = stored_data;
    g_files[file_index].size = final_size;
    g_files[file_index].dir_index = dir_index;
    if (slot < 0) {
        ++g_file_count;
    }
    serial_write("flarefs: wrote file\n");
    return 1;
}

void fs_init(const void *base, u32 size) {
    fs_reset_active();
    ext4_init_source(base, size);
    ata_init();

    if (!flarefs_mount()) {
        active_clone_source();
        serial_write("fs: using install media root\n");
    }
}

int fs_is_writable(void) {
    return g_fs_writable;
}

const char *fs_backend_name(void) {
    return g_backend_name;
}

int fs_write_file(const char *name, const char *data, usize size, int append) {
    return flarefs_write_internal(name, data, size, append);
}

int fs_remove_file(const char *name) {
    int slot = flarefs_find_slot(name);
    u32 dir_index;

    if (!g_fs_writable || slot < 0) {
        return 0;
    }

    dir_index = g_files[slot].dir_index;
    if (dir_index >= g_super.dir_entries) {
        return 0;
    }

    memset(&g_dir[dir_index], 0, sizeof(g_dir[dir_index]));
    if (!flarefs_write_directory()) {
        return 0;
    }

    for (usize i = (usize)slot + 1; i < g_file_count; ++i) {
        g_files[i - 1] = g_files[i];
    }
    --g_file_count;
    memset(&g_files[g_file_count], 0, sizeof(g_files[g_file_count]));
    return 1;
}

int fs_install(void) {
    char names[FS_MAX_ROOT_FILES][FLAREFS_NAME_LEN];
    const char *data[FS_MAX_ROOT_FILES];
    usize sizes[FS_MAX_ROOT_FILES];
    usize count = g_file_count;

    for (usize i = 0; i < count; ++i) {
        strcpy(names[i], g_files[i].name);
        data[i] = g_files[i].data;
        sizes[i] = g_files[i].size;
    }

    if (!flarefs_format()) {
        return 0;
    }
    for (usize i = 0; i < count; ++i) {
        if (!fs_write_file(names[i], data[i], sizes[i], 0)) {
            serial_write("flarefs: install copy failed\n");
            return 0;
        }
    }
    serial_write("flarefs: install complete\n");
    return 1;
}

usize fs_file_count(void) {
    return g_file_count;
}

const char *fs_file_name(usize index) {
    if (index >= g_file_count) {
        return NULL;
    }
    return g_files[index].name;
}

const char *fs_file_data(usize index, usize *size_out) {
    if (index >= g_file_count) {
        return NULL;
    }
    if (size_out) {
        *size_out = g_files[index].size;
    }
    return g_files[index].data;
}
