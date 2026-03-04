#ifndef PAGE_FUNCTIONS
#define PAGE_FUNCTIONS

#include <stdbool.h>

#include "memory_bitmap.h"

/**
    @brief Scans though the bitmap and returns the starting index of a page that is large enough for "count" amount of pages
    @param bitarr The bit array that is scanned
    @param total_bits The size of the bit array
    @param count The amount of pages/bits to be allocated
*/
static int32_t allocate_pages(uint32_t* bitarr, uint32_t total_bits, uint32_t count) {
    if (count == 0) return -1;

    int32_t start_bit = 0;

    while (start_bit < total_bits) {
        // 1. Find the next available zero
        start_bit = find_next_zero(bitarr, total_bits, start_bit);
        
        // If no zero found, or the remaining space is too small, we are out of memory
        if (start_bit == -1 || start_bit + count > total_bits) {
            return -1; 
        }

        // 2. Verify if the next 'count - 1' bits are ALSO zero
        bool enough_space = true;
        for (uint32_t i = 1; i < count; i++) {
            if (get(bitarr, start_bit + i)) {
                // Collision! We hit a used page.
                start_bit = start_bit + i + 1;
                enough_space = false;
                break; 
            }
        }

        // 3. If we found a block, mark them all as used and return the index
        if (enough_space) {
            for (uint32_t i = 0; i < count; i++) {
                set(bitarr, start_bit + i);
            }
            return start_bit;
        }
    }

    return -1; // Memory is too fragmented to satisfy the request
}

#endif  // PAGE_FUNCTIONS