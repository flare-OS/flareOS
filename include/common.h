#ifndef FLARE_COMMON_H
#define FLARE_COMMON_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef signed char i8;
typedef signed short i16;
typedef signed int i32;
typedef signed long long i64;
typedef unsigned long usize;
typedef long isize;

#define NULL ((void *)0)
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#endif
