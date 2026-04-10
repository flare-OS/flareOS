.intel_syntax noprefix
.code16

#include "layout.h"

#if STAGE2_SECTORS > 128
#error "stage2 loader supports at most 128 sectors"
#endif

.section .text
.global _start

_start:
    cli
    mov al, 'B'
    out 0xe9, al
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    mov [boot_drive], dl

    lea si, [loading_msg]
    call print_string

    call check_extensions

    mov dl, [boot_drive]
    mov word ptr [stage2_dap + 2], 64
    mov word ptr [stage2_dap + 4], 0x8000
    mov word ptr [stage2_dap + 6], 0x0000
    mov dword ptr [stage2_dap + 8], 1
    mov dword ptr [stage2_dap + 12], 0
    lea si, [stage2_dap]
    mov ah, 0x42
    int 0x13
    jc disk_error

#if STAGE2_SECTORS > 64
    mov dl, [boot_drive]
    mov word ptr [stage2_dap + 2], STAGE2_SECTORS - 64
    mov word ptr [stage2_dap + 4], 0x0000
    mov word ptr [stage2_dap + 6], 0x1000
    mov dword ptr [stage2_dap + 8], 65
    mov dword ptr [stage2_dap + 12], 0
    lea si, [stage2_dap]
    mov ah, 0x42
    int 0x13
    jc disk_error
#endif

    mov al, 'J'
    out 0xe9, al
    .byte 0xea
    .word 0x8000
    .word 0x0000

check_extensions:
    mov dl, [boot_drive]
    mov bx, 0x55aa
    mov ah, 0x41
    int 0x13
    jc no_extensions
    cmp bx, 0xaa55
    jne no_extensions
    test cx, 0x0001
    jz no_extensions
    ret

no_extensions:
    mov al, 'X'
    out 0xe9, al
    lea si, [edd_msg]
    call print_string
1:
    jmp 1b

disk_error:
    mov al, 'E'
    out 0xe9, al
    lea si, [disk_msg]
    call print_string
1:
    jmp 1b

print_string:
    lodsb
    test al, al
    jz 2f
    mov ah, 0x0e
    mov bx, 0x0007
    int 0x10
    jmp print_string
2:
    ret

loading_msg:
    .ascii "flareOS booting...\r\n\0"
disk_msg:
    .ascii "Disk read failure\r\n\0"
edd_msg:
    .ascii "INT13 extensions required\r\n\0"
boot_drive:
    .byte 0
stage2_dap:
    .byte 0x10
    .byte 0x00
    .word 0x0000
    .word 0x0000
    .word 0x0000
    .quad 0x0000000000000000

    .org 446
partition0:
    .byte 0x80
    .byte 0x00, 0x02, 0x00
    .byte 0x7f
    .byte 0xff, 0xff, 0xff
    .long 0x00000001
    .long (DISK_SECTORS - 1)
    .zero 48

.org 510
.word 0xaa55
