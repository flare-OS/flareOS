# flareOS
> [!CAUTION]
> This OS is under development. It may not boot or could corrupt data if used improperly.
> Note that it does not work on real hardware for now! flareOS hexdump reports disk read failures!

## About
flareOS is a new operating system comming to the world of shell oses, operating systems that contains no form
of GUI/UI.

Developed by a solo dev, for now it only has a few functionalities and real hardware support is
in active development.

## Makefile scripts
| Commands  | Actions                          |
|-----------|----------------------------------|
| make      | Builds the project               |
| make run  | Runs the .img using QEMU         |

## Features
- A filesystem (read-write)
- UNIX type commands
- Snake game (in-work)
- AZERTY & QWERTY Support
- Panic command
- A BSOD Screen
- Paging
- An in-work top (Linux task manager) remake
- Ethernet support
- cURL support
- A working browser (HTTP)

## Build

```bash
make
```

This produces a raw BIOS disk image at `build/os.img`.

## Write To USB With dd (FAILS!)

Build the image, then write it to a USB drive:

```bash
sudo CONFIRM=YES ./scripts/write_usb.sh build/os.img /dev/sdX
```

Or through `make`:

```bash
sudo CONFIRM=YES make usb-dd DEVICE=/dev/sdX
```

This will erase the target device.
Use the whole USB disk such as `/dev/sdb`, not a partition such as `/dev/sdb1`.

## Hardware Notes

- The image is BIOS/MBR oriented and intended for USB-HDD style boot.
- Stage 1 requires INT 13h extensions.
- The current graphics path uses a single VBE framebuffer.
- If firmware or the GPU mirrors outputs in hardware, both monitors may show the same image automatically.
- The OS does not yet enumerate or drive two independent monitors on its own.
