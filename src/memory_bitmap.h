#ifndef MEMORY_BITMAP_H
#define MEMORY_BITMAP_H

#include <stdlib.h>
#include <stdint.h>

/* === Accessor Functions === */
static inline void set(uint32_t* bitarr, uint32_t i)   { bitarr[i >> 5] |= (1UL << (i & 31)); }
static inline void clear(uint32_t* bitarr, uint32_t i) { bitarr[i >> 5] &= ~(1UL << (i & 31)); }
static inline bool get(uint32_t* bitarr, uint32_t i)   { return bitarr[i >> 5] & (1UL << (i & 31)); }

static int32_t find_first_zero(uint32_t* bitarr, size_t bit_arr_size) {
    for (uint32_t i = 0; i < bit_arr_size; i++) {
        uint32_t inv = ~bitarr[i]; // Invert to look for the lowest 1

        if (inv != 0) {
            // __builtin_ctz counts trailing zeros.
            return (i * 32) + __builtin_ctz(inv);
        }
    }
    return -1;
}

static int32_t find_next_zero(uint32_t* bitarr, uint32_t total_bits, uint32_t start_bit) {
    uint32_t start_word = start_bit >> 5;     // Equivalent to start_bit / 32
    uint32_t bit_offset = start_bit & 31;     // Equivalent to start_bit % 32
    uint32_t arr_size = (total_bits + 31) / 32;

    for (uint32_t i = start_word; i < arr_size; i++) {
        uint32_t inv = ~bitarr[i];

        // If we are checking the very first word, mask out the bits BEFORE start_bit
        // so we don't accidentally find a zero we already passed.
        if (i == start_word) {
            inv &= (~0U << bit_offset);
        }

        if (inv != 0) {
            uint32_t found_bit = (i * 32) + __builtin_ctz(inv);
            if (found_bit < total_bits) {
                return found_bit;
            }
            return -1; // Hit the end of valid bits
        }
    }
    return -1;
}

#endif  // MEMORY_BITMAP_H