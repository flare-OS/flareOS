.intel_syntax noprefix
.section .text.boot
.global _start
.extern kernel_main

_start:
    cli
    mov rsp, offset stack_top
    mov al, 'E'
    out 0xe9, al
    call kernel_main
1:
    hlt
    jmp 1b

.section .bss
.align 16
stack:
    .space 32768
stack_top:
