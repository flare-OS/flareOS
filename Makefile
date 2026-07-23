CLANG := clang
LD := ld
OBJCOPY := objcopy
MKE2FS := /usr/sbin/mke2fs
QEMU ?= qemu-system-x86_64

BUILD := build
BOOT := boot
KERNEL := kernel
INCLUDE := include
FSROOT := fsroot
DISK_SECTORS := $(shell awk '/^#define DISK_SECTORS / {print $$3}' $(BOOT)/layout.h)
FS_SECTORS := $(shell awk '/^#define FS_SECTORS / {print $$3}' $(BOOT)/layout.h)
FSROOT_FILES := $(shell find $(FSROOT) -mindepth 1 -print 2>/dev/null)

CFLAGS := -target x86_64-unknown-none-elf -ffreestanding -fno-stack-protector -fno-pic -m64 -mno-red-zone -nostdlib -nostdinc -I$(INCLUDE) -Wall -Wextra -Werror
ASFLAGS64 := -target x86_64-unknown-none-elf -ffreestanding -m64 -I$(BOOT) -I$(INCLUDE)
ASFLAGS32 := -target i386-unknown-elf -ffreestanding -m32 -I$(BOOT) -I$(INCLUDE)
LDFLAGS := -m elf_x86_64 -nostdlib -T $(KERNEL)/arch/x86_64/linker.ld

KERNEL_OBJS := \
	$(BUILD)/entry.o \
	$(BUILD)/interrupt_stubs.o \
	$(BUILD)/kernel.o \
	$(BUILD)/video.o \
	$(BUILD)/bsod.o \
	$(BUILD)/console.o \
	$(BUILD)/shell.o \
	$(BUILD)/snake.o \
	$(BUILD)/string.o \
	$(BUILD)/memory.o \
	$(BUILD)/serial.o \
	$(BUILD)/gdt.o \
	$(BUILD)/idt.o \
	$(BUILD)/paging.o \
	$(BUILD)/pic.o \
	$(BUILD)/pit.o \
	$(BUILD)/ps2.o \
	$(BUILD)/ata.o \
	$(BUILD)/keyboard.o \
	$(BUILD)/ext4.o \
	$(BUILD)/pci.o \
	$(BUILD)/rtl8139.o \
	$(BUILD)/net.o \
	$(BUILD)/editor.o \
	$(BUILD)/browser.o

all: $(BUILD)/os.img

dd-image: $(BUILD)/os.img

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/fs.bin: $(FSROOT_FILES) $(BOOT)/layout.h | $(BUILD)
	truncate -s $$(( $(FS_SECTORS) * 512 )) $@
	$(MKE2FS) -q -t ext4 -F -b 1024 -m 0 -d $(FSROOT) -O extent,^64bit,^metadata_csum,^dir_index,^has_journal $@

$(BUILD)/boot_sector.o: $(BOOT)/boot_sector.S $(BOOT)/layout.h | $(BUILD)
	$(CLANG) $(ASFLAGS32) -c -o $@ $<

$(BUILD)/stage2.o: $(BOOT)/stage2.S $(BOOT)/layout.h | $(BUILD)
	$(CLANG) $(ASFLAGS32) -c -o $@ $<

$(BUILD)/boot_sector.bin: $(BUILD)/boot_sector.o
	$(LD) -m elf_i386 -Ttext 0x7c00 --oformat binary -o $@ $<

$(BUILD)/stage2.bin: $(BUILD)/stage2.o
	$(LD) -m elf_i386 -Ttext 0x8000 --oformat binary -o $@ $<

$(BUILD)/entry.o: $(KERNEL)/arch/x86_64/entry.S | $(BUILD)
	$(CLANG) $(ASFLAGS64) -c -o $@ $<

$(BUILD)/interrupt_stubs.o: $(KERNEL)/arch/x86_64/interrupt_stubs.S | $(BUILD)
	$(CLANG) $(ASFLAGS64) -c -o $@ $<

$(BUILD)/%.o: $(KERNEL)/core/%.c | $(BUILD)
	$(CLANG) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: $(KERNEL)/arch/x86_64/%.c | $(BUILD)
	$(CLANG) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: $(KERNEL)/drivers/%.c | $(BUILD)
	$(CLANG) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: $(KERNEL)/fs/%.c | $(BUILD)
	$(CLANG) $(CFLAGS) -c -o $@ $<

$(BUILD)/kernel.elf: $(KERNEL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/os.img: $(BUILD)/boot_sector.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin $(BUILD)/fs.bin
	truncate -s $$(( $(DISK_SECTORS) * 512 )) $@
	dd if=/dev/zero of=$@ bs=512 count=$(DISK_SECTORS) conv=notrunc
	dd if=$(BUILD)/boot_sector.bin of=$@ conv=notrunc
	dd if=$(BUILD)/stage2.bin of=$@ bs=512 seek=1 conv=notrunc
	dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=65 conv=notrunc
	dd if=$(BUILD)/fs.bin of=$@ bs=512 seek=321 conv=notrunc

usb-dd: $(BUILD)/os.img scripts/write_usb.sh
	./scripts/write_usb.sh $(BUILD)/os.img $(DEVICE)

run: $(BUILD)/os.img
	$(QEMU) -no-user-config -sandbox off -drive format=raw,file=$(BUILD)/os.img,if=ide,index=0 -vga std -serial stdio -no-reboot -no-shutdown -netdev user,id=net0 -device rtl8139,netdev=net0 -m 1G

clean:
	rm -rf $(BUILD)

.PHONY: all clean dd-image usb-dd
