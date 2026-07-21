#include "kernel.h"
#include "x86_64.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static u32 pci_config_read(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 address = (1u << 31)
                | ((u32)bus << 16)
                | ((u32)slot << 11)
                | ((u32)func << 8)
                | (offset & 0xFCu);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_config_write(u8 bus, u8 slot, u8 func, u8 offset, u32 value) {
    u32 address = (1u << 31)
                | ((u32)bus << 16)
                | ((u32)slot << 11)
                | ((u32)func << 8)
                | (offset & 0xFCu);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

u32 pci_find_device(u16 vendor_id, u16 device_id) {
    for (u16 bus = 0; bus < 256; ++bus) {
        for (u8 slot = 0; slot < 32; ++slot) {
            u32 id = pci_config_read((u8)bus, slot, 0, 0x00);
            u16 vid = (u16)(id & 0xFFFFu);
            if (vid == 0xFFFFu) {
                continue;
            }
            u16 did = (u16)(id >> 16);
            if (vid == vendor_id && did == device_id) {
                return ((u32)bus << 16) | ((u32)slot << 8);
            }
        }
    }
    return 0xFFFFFFFFu;
}

u32 pci_read_bar0(u32 pci_loc) {
    u8 bus = (u8)(pci_loc >> 16);
    u8 slot = (u8)(pci_loc >> 8);
    return pci_config_read(bus, slot, 0, 0x10);
}

u16 pci_read_command(u32 pci_loc) {
    u8 bus = (u8)(pci_loc >> 16);
    u8 slot = (u8)(pci_loc >> 8);
    u32 value = pci_config_read(bus, slot, 0, 0x04);
    return (u16)(value & 0xFFFFu);
}

void pci_write_command(u32 pci_loc, u16 command) {
    u8 bus = (u8)(pci_loc >> 16);
    u8 slot = (u8)(pci_loc >> 8);
    u32 value = pci_config_read(bus, slot, 0, 0x04);
    value = (value & 0xFFFF0000u) | command;
    pci_config_write(bus, slot, 0, 0x04, value);
}

u8 pci_read_irq(u32 pci_loc) {
    u8 bus = (u8)(pci_loc >> 16);
    u8 slot = (u8)(pci_loc >> 8);
    u32 value = pci_config_read(bus, slot, 0, 0x3C);
    return (u8)(value & 0xFFu);
}
