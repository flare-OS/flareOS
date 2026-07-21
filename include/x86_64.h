#ifndef FLARE_X86_64_H
#define FLARE_X86_64_H

#include "common.h"

static inline void io_wait(void) {
    __asm__ volatile("outb %%al, $0x80" : : "a"(0));
}

static inline void outb(u16 port, u8 value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void outw(u16 port, u16 value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline u8 inb(u16 port) {
    u8 value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline u16 inw(u16 port) {
    u16 value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outl(u16 port, u32 value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline u32 inl(u16 port) {
    u32 value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void lidt(const void *ptr) {
    __asm__ volatile("lidt (%0)" : : "r"(ptr));
}

static inline void lgdt(const void *ptr) {
    __asm__ volatile("lgdt (%0)" : : "r"(ptr));
}

static inline void sti(void) {
    __asm__ volatile("sti");
}

static inline void cli(void) {
    __asm__ volatile("cli");
}

static inline void hlt(void) {
    __asm__ volatile("hlt");
}

static inline u64 read_cr2(void) {
    u64 value;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

static inline void write_cr3(u64 value) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(value) : "memory");
}

static inline u64 read_rflags(void) {
    u64 flags;
    __asm__ volatile("pushfq; pop %0" : "=r"(flags));
    return flags;
}

#endif
