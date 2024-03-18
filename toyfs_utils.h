#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define nullptr             ((void*)0)
#define array_length(array) (sizeof(array) / sizeof(array[0]))

#define USE_LOG 0

#if USE_LOG
#define util_logger(...)    printf(__VA_ARGS__)
#define util_error(...)     printf("ERROR: "), printf(__VA_ARGS__), exit(0)
#define util_display(value) printf("%s = 0x%x, %d\n", #value, value, value)
#else
#define util_logger(...)
#endif


#define util_bitmap_set(u32bitmap, pos) u32bitmap[pos >> 5] |= ((uint32_t)0x1 << (pos & 0x1F))
#define util_bitmap_clr(u32bitmap, pos) u32bitmap[pos >> 5] &= ~((uint32_t)0x1 << (pos & 0x1F))
#define util_bitmap_chk(u32bitmap, pos) (u32bitmap[pos >> 5] & ((uint32_t)0x1 << (pos & 0x1F)))


void     util_dump(uint8_t* block, int size);
void     util_name2sfn(const char* name, char* sfn);
void     util_sfn2name(const char* sfn, char* name);
int      util_get_1st_subpath(const char* subpath, char* name);
uint32_t util_get_value_from_block(uint8_t* block, int ofs, int size);
