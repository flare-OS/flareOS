#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <image> <device>" >&2
    echo "example: sudo $0 build/os.img /dev/sdX" >&2
    exit 1
fi

IMAGE=$1
DEVICE=$2

if [[ ! -f "$IMAGE" ]]; then
    echo "image not found: $IMAGE" >&2
    exit 1
fi

if [[ ! -b "$DEVICE" ]]; then
    echo "device is not a block device: $DEVICE" >&2
    exit 1
fi

DEVICE_TYPE=$(lsblk -dnpo TYPE "$DEVICE" 2>/dev/null || true)
if [[ "$DEVICE_TYPE" != "disk" ]]; then
    echo "target must be a whole-disk device, not a partition: $DEVICE" >&2
    echo "use /dev/sdX, not /dev/sdX1" >&2
    exit 1
fi

if [[ $(id -u) -ne 0 ]]; then
    echo "run as root to write to $DEVICE" >&2
    exit 1
fi

if [[ "${CONFIRM:-}" != "YES" ]]; then
    echo "refusing to write without CONFIRM=YES" >&2
    lsblk -dpno NAME,SIZE,MODEL "$DEVICE" || true
    echo "example: sudo CONFIRM=YES $0 $IMAGE $DEVICE" >&2
    exit 1
fi

echo "writing $IMAGE to $DEVICE"
lsblk -dpno NAME,SIZE,MODEL "$DEVICE" || true
dd if="$IMAGE" of="$DEVICE" bs=4M conv=fsync status=progress
sync
echo "done"
