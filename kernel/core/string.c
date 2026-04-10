#include "kernel.h"

void *memcpy(void *dest, const void *src, usize count) {
    u8 *d = (u8 *)dest;
    const u8 *s = (const u8 *)src;
    while (count--) {
        *d++ = *s++;
    }
    return dest;
}

void *memset(void *dest, int value, usize count) {
    u8 *d = (u8 *)dest;
    while (count--) {
        *d++ = (u8)value;
    }
    return dest;
}

int memcmp(const void *lhs, const void *rhs, usize count) {
    const u8 *a = (const u8 *)lhs;
    const u8 *b = (const u8 *)rhs;
    while (count--) {
        if (*a != *b) {
            return (int)*a - (int)*b;
        }
        ++a;
        ++b;
    }
    return 0;
}

usize strlen(const char *s) {
    usize len = 0;
    while (s[len]) {
        ++len;
    }
    return len;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, usize count) {
    while (count && *a && (*a == *b)) {
        ++a;
        ++b;
        --count;
    }
    if (!count) {
        return 0;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

char *strcpy(char *dest, const char *src) {
    char *out = dest;
    while ((*dest++ = *src++)) {
    }
    return out;
}

char *strncpy(char *dest, const char *src, usize count) {
    char *out = dest;
    while (count && *src) {
        *dest++ = *src++;
        --count;
    }
    while (count--) {
        *dest++ = '\0';
    }
    return out;
}
