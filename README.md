# flareOS

## Build

```bash
make
```

This produces a raw BIOS disk image at `build/os.img`.

## Write To USB With dd

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
