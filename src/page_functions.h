#ifndef PAGE_FUNCTIONS
#define PAGE_FUNCTIONS

#include <stdbool.h>

#include "memory_bitmap.h"



// --- Constants & Memory ---
#define VIRTUAL_MEMORY_SIZE (40 * 1024 * 1024)
#define PAGE_SIZE 4096
#define NUM_PAGES (VIRTUAL_MEMORY_SIZE / PAGE_SIZE)
#define VIRTUAL_FILE_PAGE_SIZE 4096

// Constant for the base address assigned in the linker script( in the pico code ).
#define VIRTUAL_MEMORY_BASE 0x20082000

/**
    @brief Scans through the bitmap and returns the starting virtual address of a block that is large enough for the requested size
    @param bitarr The bit array that is scanned
    @param sizes_arr The parallel array used to store the size of each allocation
    @param count The amount of pages to be allocated
    @return The allocated page idx or -1 if out of memory
*/
static int32_t allocate_pages(uint32_t* bitarr, uint32_t* sizes_arr, uint32_t count) {
    if (count == 0) return 0;

    int32_t page_idx = 0;

    while (page_idx != -1 && (uint32_t)page_idx < NUM_PAGES) {
        page_idx = find_next_zero(bitarr, NUM_PAGES, (uint32_t)page_idx);
        
        // If no zero found, or the remaining space is too small, we are out of memory
        if (page_idx == -1 || (uint32_t)page_idx + count > NUM_PAGES) {
            return -1;
        }

        bool block_is_free = true;

        // Verify that the free block has enough space for our requested allocation block
        for (uint32_t i = 1; i < count; i++) {
            if (get(bitarr, (uint32_t)page_idx + i)) {
                page_idx = page_idx + i + 1;
                block_is_free = false;
                break; 
            }
        }

        if (!block_is_free) continue; // Restart the while loop with new page_idx

        // Allocate the block
        for (uint32_t i = 0; i < count; i++) {
            set(bitarr, (uint32_t)page_idx + i);
        }
        
        sizes_arr[page_idx] = count;
        return page_idx;
    }

    return -1; // Memory is too fragmented
}

/**
    @brief Frees a previously allocated contiguous block of pages based on its virtual address.
    @param bitarr The bit array that is scanned
    @param sizes_arr The parallel array storing the sizes of allocations
    @param vaddr The starting virtual address of the pages to free
*/
static void free_pages(uint32_t* bitarr, uint32_t* sizes_arr, uint32_t vaddr) {
    if (vaddr < VIRTUAL_MEMORY_BASE) {
        return;
    }

    uint32_t page_idx = (vaddr - VIRTUAL_MEMORY_BASE) / PAGE_SIZE;

    // Look up how many pages were originally allocated at this index
    uint32_t count = sizes_arr[page_idx];
    if (count == 0) return; // Nothing to free

    // Clear the bits in the bitmap
    for (uint32_t i = 0; i < count; i++) {
        clear(bitarr, page_idx + i);
    }
    
    // Clear the size metadata to prevent double-free issues
    sizes_arr[page_idx] = 0; 
}

#endif  // PAGE_FUNCTIONS