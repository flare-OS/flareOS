#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FS_MAGIC 0x46534631u

typedef struct {
    uint32_t magic;
    uint32_t file_count;
} FsHeader;

typedef struct {
    char name[32];
    uint32_t offset;
    uint32_t size;
} FsEntry;

typedef struct {
    const char *name;
    const char *data;
} FileSpec;

static const FileSpec files[] = {
    {"README.TXT", "mannn idk\n"},
    {"NOTES.TXT", "Mouse: PS/2 IRQ12\nKeyboard: IRQ1\n"},
    {"APPS.TXT", "Terminal\nFiles\n"},
};

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <out>\n", argv[0]);
        return 1;
    }

    FILE *fp = fopen(argv[1], "wb");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    FsHeader header = {FS_MAGIC, (uint32_t)(sizeof(files) / sizeof(files[0]))};
    FsEntry entries[sizeof(files) / sizeof(files[0])] = {0};
    uint32_t offset = sizeof(header) + sizeof(entries);

    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        strncpy(entries[i].name, files[i].name, sizeof(entries[i].name) - 1);
        entries[i].offset = offset;
        entries[i].size = (uint32_t)strlen(files[i].data);
        offset += entries[i].size;
    }

    fwrite(&header, sizeof(header), 1, fp);
    fwrite(entries, sizeof(entries), 1, fp);
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        fwrite(files[i].data, entries[i].size, 1, fp);
    }
    fclose(fp);
    return 0;
}
