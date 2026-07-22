#include "kernel.h"
#include "x86_64.h"

/* RTL8139 Register Offsets */
#define RTL_MAC0       0x00
#define RTL_MAR0       0x08
#define RTL_RBSTART    0x30
#define RTL_CMD        0x37
#define RTL_CAPR       0x38
#define RTL_CBR        0x3A
#define RTL_IMR        0x3C
#define RTL_ISR        0x3E
#define RTL_TCR        0x40
#define RTL_RCR        0x44
#define RTL_TSD0       0x10
#define RTL_TSAD0      0x20

/* Command register bits */
#define RTL_CMD_RESET   0x10
#define RTL_CMD_RE      0x08
#define RTL_CMD_TE      0x04
#define RTL_CMD_BUFE    0x01

/* Receive config bits */
#define RTL_RCR_AAP     0x01
#define RTL_RCR_APM     0x02
#define RTL_RCR_AM      0x04
#define RTL_RCR_AB      0x08
#define RTL_RCR_WRAP    0x80

/* ISR bits */
#define RTL_ISR_ROK     0x01
#define RTL_ISR_TOK     0x04

/* RX buffer size: 8KB + 16 bytes header + 1500 wrap */
#define RTL_RX_BUF_SIZE (8192 + 16 + 1536)
#define RTL_RX_BUF_PAD  16

static u16 g_io_base = 0;
static u8 *g_rx_buffer = NULL;
static u16 g_rx_offset = 0;
static u8 g_mac[6] = {0};
static int g_nic_found = 0;
static u32 g_tx_desc = 0;

/* PCI IDs for RTL8139 */
#define RTL8139_VENDOR 0x10EC
#define RTL8139_DEVICE 0x8139

static u32 pci_loc = 0;

static void rtl_write8(u16 reg, u8 value) { outb(g_io_base + reg, value); }
static void rtl_write16(u16 reg, u16 value) { outw(g_io_base + reg, value); }
static void rtl_write32(u16 reg, u32 value) { outl(g_io_base + reg, value); }
static u8 rtl_read8(u16 reg) { return inb(g_io_base + reg); }
static u32 rtl_read32(u16 reg) { return inl(g_io_base + reg); }

int rtl8139_init(void) {
    serial_write("rtl8139: scanning...\n");

    pci_loc = pci_find_device(RTL8139_VENDOR, RTL8139_DEVICE);
    if (pci_loc == 0xFFFFFFFFu) {
        serial_write("rtl8139: not found\n");
        return 0;
    }

    u32 bar0 = pci_read_bar0(pci_loc);
    g_io_base = (u16)(bar0 & 0xFFFCu);
    if (g_io_base == 0) {
        serial_write("rtl8139: no IO BAR\n");
        return 0;
    }

    /* Enable bus mastering + IO space */
    u16 cmd = pci_read_command(pci_loc);
    cmd |= 0x05;
    pci_write_command(pci_loc, cmd);

    /* Power on */
    rtl_write8(0x52, 0x00);

    /* Software reset */
    rtl_write8(RTL_CMD, RTL_CMD_RESET);
    for (int i = 0; i < 1000; ++i) {
        if ((rtl_read8(RTL_CMD) & RTL_CMD_RESET) == 0) {
            break;
        }
        io_wait();
    }

    /* Allocate RX buffer */
    g_rx_buffer = (u8 *)kmalloc(RTL_RX_BUF_SIZE);
    if (!g_rx_buffer) {
        serial_write("rtl8139: rx buffer alloc failed\n");
        return 0;
    }
    memset(g_rx_buffer, 0, RTL_RX_BUF_SIZE);

    /* Set RX buffer address */
    rtl_write32(RTL_RBSTART, (u32)(usize)g_rx_buffer);

    /* Enable RX and TX */
    rtl_write8(RTL_CMD, RTL_CMD_RE | RTL_CMD_TE);

    /* Configure receive: accept broadcast, multicast, physical match, wrap */
    rtl_write32(RTL_RCR, RTL_RCR_AAP | RTL_RCR_APM | RTL_RCR_AM | RTL_RCR_AB | RTL_RCR_WRAP);

    /* Accept all multicast */
    rtl_write32(RTL_MAR0, 0xFFFFFFFFu);
    rtl_write32(RTL_MAR0 + 4, 0xFFFFFFFFu);

    /* Read MAC address */
    for (int i = 0; i < 6; ++i) {
        g_mac[i] = rtl_read8(RTL_MAC0 + (u16)i);
    }

    g_rx_offset = 0;
    g_nic_found = 1;

    serial_write("rtl8139: online io=");
    {
        char hex[9];
        static const char digits[] = "0123456789abcdef";
        for (int s = 12; s >= 0; s -= 4) {
            hex[3 - s/4] = digits[((u32)g_io_base >> s) & 0xF];
        }
        hex[4] = '\0';
        serial_write(hex);
    }
    serial_write(" mac=");
    for (int i = 0; i < 6; ++i) {
        char hex[3];
        static const char digits[] = "0123456789abcdef";
        hex[0] = digits[(g_mac[i] >> 4) & 0xF];
        hex[1] = digits[g_mac[i] & 0xF];
        hex[2] = '\0';
        serial_write(hex);
        if (i < 5) serial_write(":");
    }
    serial_write("\n");

    return 1;
}

int rtl8139_present(void) {
    return g_nic_found;
}

void rtl8139_get_mac(u8 *mac_out) {
    memcpy(mac_out, g_mac, 6);
}

int rtl8139_send_packet(const void *data, u16 length) {
    if (!g_nic_found || !data || length < 14 || length > 1518) {
        return 0;
    }

    u32 desc = g_tx_desc;
    u16 tsad = RTL_TSAD0 + (u16)(desc * 4);
    u16 tsd  = RTL_TSD0 + (u16)(desc * 4);

    u8 *tx_buf = (u8 *)kmalloc(length + 16);
    if (!tx_buf) {
        return 0;
    }
    memcpy(tx_buf, data, length);

    while (length < 60) {
        tx_buf[length++] = 0;
    }

    rtl_write32(tsad, (u32)(usize)tx_buf);
    rtl_write32(tsd, (u32)length);

    for (int i = 0; i < 100000; ++i) {
        u32 status = rtl_read32(tsd);
        if (status & 0x8000u) {
            g_tx_desc = (g_tx_desc + 1) & 3;
            return 1;
        }
    }

    return 0;
}

/* Returns packet length or 0 if none available. data_out points into RX buffer. */
u16 rtl8139_poll_packet(const u8 **data_out) {
    if (!g_nic_found || !data_out) {
        return 0;
    }
    *data_out = NULL;

    /* Check if buffer is empty */
    if (rtl_read8(RTL_CMD) & RTL_CMD_BUFE) {
        return 0;
    }

    /* RX packet header: 2 bytes status, 2 bytes length */
    u16 rx_status = *(volatile u16 *)(g_rx_buffer + g_rx_offset);
    u16 rx_length = *(volatile u16 *)(g_rx_buffer + g_rx_offset + 2);

    /* Validate the header before trusting it */
    u16 next_offset = (u16)((g_rx_offset + rx_length + 4 + 3) & ~3u);
    if (rx_length < 60 || rx_length > 1518 || next_offset >= RTL_RX_BUF_SIZE) {
        /* Corrupt header: reset the RX buffer state */
        g_rx_offset = 0;
        rtl_write16(RTL_CAPR, (u16)-16);
        rtl_write16(RTL_ISR, RTL_ISR_ROK);
        return 0;
    }

    /* Check for valid packet */
    if (!(rx_status & 0x01)) { /* ROK bit */
        g_rx_offset = next_offset;
        if (g_rx_offset >= RTL_RX_BUF_SIZE - RTL_RX_BUF_PAD) {
            g_rx_offset = 0;
        }
        rtl_write16(RTL_CAPR, (u16)(g_rx_offset - 16));
        return 0;
    }

    *data_out = g_rx_buffer + g_rx_offset + 4;
    u16 packet_len = (u16)(rx_length - 4); /* Subtract CRC */

    /* Advance offset for next packet */
    g_rx_offset = next_offset;
    if (g_rx_offset >= RTL_RX_BUF_SIZE - RTL_RX_BUF_PAD) {
        g_rx_offset = 0;
    }
    rtl_write16(RTL_CAPR, (u16)(g_rx_offset - 16));

    /* Acknowledge RX interrupt */
    rtl_write16(RTL_ISR, RTL_ISR_ROK);

    return packet_len;
}
