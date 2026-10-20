/*
 * types.h - fixed width types used across the engine.
 *
 * The original is a 32 bit MSVC build: int, long and pointers are 32 bits.
 * Fields stored in data files or compared by width use these types.
 */

#ifndef IGN_TYPES_H
#define IGN_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;

#endif /* IGN_TYPES_H */
