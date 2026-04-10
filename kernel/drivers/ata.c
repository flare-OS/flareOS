#include "kernel.h"
#include "x86_64.h"

#define ATA_IO_BASE 0x1f0
#define ATA_CTRL_BASE 0x3f6

#define ATA_REG_DATA 0
#define ATA_REG_ERROR 1
#define ATA_REG_SECTOR_COUNT 2
#define ATA_REG_LBA0 3
#define ATA_REG_LBA1 4
#define ATA_REG_LBA2 5
#define ATA_REG_DRIVE 6
#define ATA_REG_STATUS 7
#define ATA_REG_COMMAND 7

#define ATA_CMD_READ_SECTOR 0x20
#define ATA_CMD_WRITE_SECTOR 0x30
#define ATA_CMD_CACHE_FLUSH 0xe7
#define ATA_CMD_IDENTIFY 0xec

#define ATA_STATUS_ERR 0x01
#define ATA_STATUS_DRQ 0x08
#define ATA_STATUS_DF 0x20
#define ATA_STATUS_DRDY 0x40
#define ATA_STATUS_BSY 0x80

static int g_ata_present = 0;

static u8 ata_status(void) {
    return inb(ATA_IO_BASE + ATA_REG_STATUS);
}

static int ata_wait_not_busy(void) {
    for (u32 i = 0; i < 1000000u; ++i) {
        if ((ata_status() & ATA_STATUS_BSY) == 0) {
            return 1;
        }
    }
    return 0;
}

static int ata_poll_drq(void) {
    for (u32 i = 0; i < 1000000u; ++i) {
        u8 status = ata_status();

        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) {
            return 0;
        }
        if ((status & ATA_STATUS_BSY) == 0 && (status & ATA_STATUS_DRQ) != 0) {
            return 1;
        }
    }
    return 0;
}

static int ata_select_lba28(u32 lba) {
    if (lba >= (1u << 28)) {
        return 0;
    }
    if (!ata_wait_not_busy()) {
        return 0;
    }

    outb(ATA_CTRL_BASE, 0x00);
    outb(ATA_IO_BASE + ATA_REG_DRIVE, (u8)(0xe0u | ((lba >> 24) & 0x0fu)));
    io_wait();
    outb(ATA_IO_BASE + ATA_REG_SECTOR_COUNT, 1);
    outb(ATA_IO_BASE + ATA_REG_LBA0, (u8)(lba & 0xffu));
    outb(ATA_IO_BASE + ATA_REG_LBA1, (u8)((lba >> 8) & 0xffu));
    outb(ATA_IO_BASE + ATA_REG_LBA2, (u8)((lba >> 16) & 0xffu));
    return 1;
}

void ata_init(void) {
    u16 identify[256];

    g_ata_present = 0;
    outb(ATA_CTRL_BASE, 0x00);
    outb(ATA_IO_BASE + ATA_REG_DRIVE, 0xa0);
    io_wait();
    outb(ATA_IO_BASE + ATA_REG_SECTOR_COUNT, 0);
    outb(ATA_IO_BASE + ATA_REG_LBA0, 0);
    outb(ATA_IO_BASE + ATA_REG_LBA1, 0);
    outb(ATA_IO_BASE + ATA_REG_LBA2, 0);
    outb(ATA_IO_BASE + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    if (ata_status() == 0) {
        serial_write("ata: no drive\n");
        return;
    }
    if (!ata_poll_drq()) {
        serial_write("ata: identify failed\n");
        return;
    }

    for (int i = 0; i < 256; ++i) {
        identify[i] = inw(ATA_IO_BASE + ATA_REG_DATA);
    }
    if ((identify[49] & (1u << 9)) == 0) {
        serial_write("ata: drive present without lba support\n");
        return;
    }

    g_ata_present = 1;
    serial_write("ata: primary master online\n");
}

int ata_present(void) {
    return g_ata_present;
}

int ata_read_sector(u32 lba, void *buffer) {
    u16 *dst = (u16 *)buffer;

    if (!g_ata_present || !buffer || !ata_select_lba28(lba)) {
        return 0;
    }

    outb(ATA_IO_BASE + ATA_REG_COMMAND, ATA_CMD_READ_SECTOR);
    if (!ata_poll_drq()) {
        return 0;
    }

    for (int i = 0; i < 256; ++i) {
        dst[i] = inw(ATA_IO_BASE + ATA_REG_DATA);
    }
    return 1;
}

int ata_write_sector(u32 lba, const void *buffer) {
    const u16 *src = (const u16 *)buffer;

    if (!g_ata_present || !buffer || !ata_select_lba28(lba)) {
        return 0;
    }

    outb(ATA_IO_BASE + ATA_REG_COMMAND, ATA_CMD_WRITE_SECTOR);
    if (!ata_poll_drq()) {
        return 0;
    }

    for (int i = 0; i < 256; ++i) {
        outw(ATA_IO_BASE + ATA_REG_DATA, src[i]);
    }
    return ata_flush();
}

int ata_flush(void) {
    if (!g_ata_present) {
        return 0;
    }

    outb(ATA_IO_BASE + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    return ata_wait_not_busy() && (ata_status() & (ATA_STATUS_ERR | ATA_STATUS_DF)) == 0;
}
