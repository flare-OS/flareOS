.intel_syntax noprefix
.code16

#include "layout.h"

.equ BOOTINFO_ADDR, 0x7000
.equ FONT_BUFFER_ADDR, 0x5000
.equ VBE_INFO_ADDR, 0x6000
.equ VBE_MODEINFO_ADDR, 0x6200
.equ E820_ENTRY_ADDR, 0x6e00
.equ STAGE2_LOAD_ADDR, 0x8000
.equ KERNEL_LOAD_ADDR, 0x10000
.equ FS_LOAD_ADDR, 0x90000

.section .text
.global stage2_start

stage2_start:
    cli
    mov al, 'S'
    out 0xe9, al
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    mov [boot_drive], dl

    mov al, '1'
    out 0xe9, al
    call enable_a20
    mov al, '2'
    out 0xe9, al
    call init_bootinfo
    mov al, 'I'
    out 0xe9, al
    call detect_memory
    call set_graphics_mode
    call load_font
    mov al, 'V'
    out 0xe9, al
    call load_kernel
    mov al, 'K'
    out 0xe9, al
    call load_fs
    mov al, 'F'
    out 0xe9, al
    call enter_protected_mode
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

init_bootinfo:
    mov di, BOOTINFO_ADDR
    mov cx, 36
    xor ax, ax
    rep stosw
    mov dword ptr [BOOTINFO_ADDR + 0], 0x464c4152
    xor eax, eax
    mov al, [boot_drive]
    mov dword ptr [BOOTINFO_ADDR + 4], eax
    mov dword ptr [BOOTINFO_ADDR + 44], 60
    ret

detect_memory:
    push es
    xor ax, ax
    mov es, ax
    mov dword ptr [BOOTINFO_ADDR + 48], 0
    mov dword ptr [BOOTINFO_ADDR + 52], 0
    xor ebx, ebx
1:
    mov eax, 0xe820
    mov edx, 0x534d4150
    mov ecx, 24
    mov di, E820_ENTRY_ADDR
    int 0x15
    jc 2f
    cmp eax, 0x534d4150
    jne 2f
    cmp dword ptr [E820_ENTRY_ADDR + 16], 1
    jne 3f
    mov eax, dword ptr [E820_ENTRY_ADDR + 8]
    add dword ptr [BOOTINFO_ADDR + 48], eax
    mov eax, dword ptr [E820_ENTRY_ADDR + 12]
    adc dword ptr [BOOTINFO_ADDR + 52], eax
3:
    test ebx, ebx
    jnz 1b
    pop es
    ret
2:
    mov ah, 0x88
    int 0x15
    jc 4f
    movzx eax, ax
    add eax, 1024
    shl eax, 10
    mov dword ptr [BOOTINFO_ADDR + 48], eax
    mov dword ptr [BOOTINFO_ADDR + 52], 0
4:
    pop es
    ret

set_graphics_mode:
    push ds
    push es
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov di, VBE_INFO_ADDR
    mov dword ptr [di], 0x32454256
    mov ax, 0x4f00
    int 0x10
    cmp ax, 0x004f
    jne 9f

    mov word ptr [best_mode], 0
    mov dword ptr [best_area], 0
    mov word ptr [fallback_mode], 0
    mov dword ptr [fallback_area], 0
    mov ax, word ptr [VBE_INFO_ADDR + 16]
    mov word ptr [mode_list_seg], ax
    mov si, word ptr [VBE_INFO_ADDR + 14]
1:
    push ds
    mov ax, word ptr [mode_list_seg]
    mov ds, ax
    mov cx, word ptr [si]
    pop ds
    cmp cx, 0xffff
    je 2f
    add si, 2

    xor ax, ax
    mov es, ax
    mov di, VBE_MODEINFO_ADDR
    mov ax, 0x4f01
    int 0x10
    cmp ax, 0x004f
    jne 1b

    mov ax, word ptr [VBE_MODEINFO_ADDR + 0]
    and ax, 0x0091
    cmp ax, 0x0091
    jne 1b
    mov al, byte ptr [VBE_MODEINFO_ADDR + 25]
    cmp al, 24
    jb 1b
    mov al, byte ptr [VBE_MODEINFO_ADDR + 27]
    cmp al, 4
    je 3f
    cmp al, 6
    jne 1b
3:
    mov eax, dword ptr [VBE_MODEINFO_ADDR + 40]
    test eax, eax
    jz 1b

    movzx eax, word ptr [VBE_MODEINFO_ADDR + 18]
    cmp ax, 1024
    ja 4f
    movzx edx, word ptr [VBE_MODEINFO_ADDR + 20]
    cmp dx, 768
    ja 4f
    mul edx
    cmp eax, dword ptr [best_area]
    jbe 1b
    mov dword ptr [best_area], eax
    mov word ptr [best_mode], cx
    jmp 1b
4:
    movzx eax, word ptr [VBE_MODEINFO_ADDR + 18]
    movzx edx, word ptr [VBE_MODEINFO_ADDR + 20]
    mul edx
    cmp eax, dword ptr [fallback_area]
    jbe 1b
    mov dword ptr [fallback_area], eax
    mov word ptr [fallback_mode], cx
    jmp 1b
2:
    mov bx, word ptr [best_mode]
    cmp bx, 0
    jne 5f
    mov bx, word ptr [fallback_mode]
5:
    cmp bx, 0
    je 9f
    mov dx, bx
    or bx, 0x4000
    mov ax, 0x4f02
    int 0x10
    cmp ax, 0x004f
    jne 9f

    mov cx, dx
    xor ax, ax
    mov es, ax
    mov di, VBE_MODEINFO_ADDR
    mov ax, 0x4f01
    int 0x10
    cmp ax, 0x004f
    jne 9f

    movzx eax, word ptr [VBE_MODEINFO_ADDR + 18]
    mov dword ptr [BOOTINFO_ADDR + 8], eax
    movzx eax, word ptr [VBE_MODEINFO_ADDR + 20]
    mov dword ptr [BOOTINFO_ADDR + 12], eax
    movzx eax, word ptr [VBE_MODEINFO_ADDR + 16]
    mov dword ptr [BOOTINFO_ADDR + 16], eax
    movzx eax, byte ptr [VBE_MODEINFO_ADDR + 25]
    mov dword ptr [BOOTINFO_ADDR + 20], eax
    mov eax, dword ptr [VBE_MODEINFO_ADDR + 40]
    mov dword ptr [BOOTINFO_ADDR + 24], eax
    mov dword ptr [BOOTINFO_ADDR + 28], 0
    pop es
    pop ds
    ret
9:
    pop es
    pop ds
    mov ax, 0x0013
    int 0x10
    mov dword ptr [BOOTINFO_ADDR + 8], 320
    mov dword ptr [BOOTINFO_ADDR + 12], 200
    mov dword ptr [BOOTINFO_ADDR + 16], 320
    mov dword ptr [BOOTINFO_ADDR + 20], 8
    mov dword ptr [BOOTINFO_ADDR + 24], 0x000a0000
    mov dword ptr [BOOTINFO_ADDR + 28], 0
    ret

load_font:
    push ds
    push es
    mov ax, 0x1130
    mov bh, 0x06
    int 0x10
    mov si, bp
    mov di, FONT_BUFFER_ADDR
    mov ax, es
    mov ds, ax
    xor ax, ax
    mov es, ax
    mov cx, 2048
    rep movsw
    pop es
    pop ds
    mov dword ptr [BOOTINFO_ADDR + 56], FONT_BUFFER_ADDR
    mov dword ptr [BOOTINFO_ADDR + 60], 0
    mov dword ptr [BOOTINFO_ADDR + 64], 16
    mov dword ptr [BOOTINFO_ADDR + 68], 0
    ret

load_kernel:
    lea si, [kernel_msg]
    call print_string
    mov dx, KERNEL_LOAD_ADDR >> 4
    mov cx, KERNEL_SECTORS
    mov ax, KERNEL_LBA
    call load_sectors
    ret

load_fs:
    mov dx, FS_LOAD_ADDR >> 4
    mov cx, FS_SECTORS
    mov ax, FS_LBA
    call load_sectors
    mov dword ptr [BOOTINFO_ADDR + 32], FS_LOAD_ADDR
    mov dword ptr [BOOTINFO_ADDR + 36], 0
    mov dword ptr [BOOTINFO_ADDR + 40], FS_SECTORS * 512
    ret

load_sectors:
    push es
    mov es, dx
    xor bx, bx
.next:
    test cx, cx
    jz 3f
    mov word ptr [sector_dap + 2], 0x0001
    mov word ptr [sector_dap + 4], bx
    xor edx, edx
    mov dx, ax
    mov dword ptr [sector_dap + 8], edx
    mov dword ptr [sector_dap + 12], 0
    mov dx, es
    mov word ptr [sector_dap + 6], dx
    lea si, [sector_dap]
    push ax
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    pop ax
    jc disk_fail
    inc ax
    add bx, 512
    jnc 1f
    push ax
    mov ax, es
    add ax, 0x1000
    mov es, ax
    pop ax
1:
    dec cx
    jmp .next
3:
    pop es
    ret

disk_fail:
    mov al, 'D'
    out 0xe9, al
    lea si, [fail_msg]
    call print_string
    cli
    hlt

enable_a20:
    mov al, 'A'
    out 0xe9, al
    in al, 0x92
    and al, 0xfe
    or al, 0x02
    mov ah, al
    mov al, 'B'
    out 0xe9, al
    mov al, 'C'
    out 0xe9, al
    mov al, ah
    out 0x92, al
    mov al, 'D'
    out 0xe9, al
    ret

.align 8
gdt64:
    .quad 0x0000000000000000
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
    .quad 0x00af9a000000ffff
    .quad 0x00af92000000ffff
gdt64_end:
gdt64_ptr:
    .word (gdt64_end - gdt64 - 1)
    .long gdt64

.align 4096
pml4_table:
    .quad pdpt_table + STAGE2_LOAD_ADDR + 0x003
    .fill 511, 8, 0
.align 4096
pdpt_table:
    .quad pd0_table + STAGE2_LOAD_ADDR + 0x003
    .quad pd1_table + STAGE2_LOAD_ADDR + 0x003
    .quad pd2_table + STAGE2_LOAD_ADDR + 0x003
    .quad pd3_table + STAGE2_LOAD_ADDR + 0x003
    .fill 508, 8, 0
.align 4096
pd0_table:
    .rept 512
    .quad ((. - pd0_table) / 8 * 0x200000) | 0x083
    .endr
.align 4096
pd1_table:
    .rept 512
    .quad (0x40000000 + ((. - pd1_table) / 8 * 0x200000)) | 0x083
    .endr
.align 4096
pd2_table:
    .rept 512
    .quad (0x80000000 + ((. - pd2_table) / 8 * 0x200000)) | 0x083
    .endr
.align 4096
pd3_table:
    .rept 512
    .quad (0xc0000000 + ((. - pd3_table) / 8 * 0x200000)) | 0x083
    .endr

enter_protected_mode:
    cli
    lgdt [gdt64_ptr]
    mov eax, cr0
    or eax, 0x00000001
    mov cr0, eax
    .byte 0xea
    .word protected_mode_start
    .word 0x08

.code32
protected_mode_start:
    mov al, 'P'
    out 0xe9, al
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov eax, offset pml4_table
    mov cr3, eax

    mov ecx, 0xc0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    .byte 0xea
    .long long_mode_start
    .word 0x18

.code64
long_mode_start:
    mov al, 'L'
    out 0xe9, al
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov rsp, 0x9f000
    mov rdi, BOOTINFO_ADDR
    mov rax, KERNEL_LOAD_ADDR
    jmp rax

.code16
kernel_msg:
    .ascii "Loading kernel...\r\n\0"
fail_msg:
    .ascii "Stage2 disk failure\r\n\0"
boot_drive:
    .byte 0
mode_list_seg:
    .word 0
best_mode:
    .word 0
best_area:
    .long 0
fallback_mode:
    .word 0
fallback_area:
    .long 0
sector_dap:
    .byte 0x10
    .byte 0x00
    .word 0x0000
    .word 0x0000
    .word 0x0000
    .quad 0x0000000000000000
