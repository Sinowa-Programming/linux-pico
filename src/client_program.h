#ifndef CLIENT_PROGRAM_H
#define CLIENT_PROGRAM_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/**
 * @brief Open a binary client program file for reading
 * @param filename Path to the binary file
 * @return FILE handle on success, NULL on failure
 */
FILE* open_client_binary(const char* filename);

/**
 * @brief Extract a single page from an opened binary file
 * @param bin_file FILE handle from open_client_binary
 * @param page_id The page number to extract
 * @param buffer Output buffer to store the page data
 * @param page_size Size of each page (typically 4096)
 * @return Number of bytes read, -1 on failure
 */
int32_t extract_page_from_binary(FILE* bin_file, uint32_t page_id, uint8_t* buffer, uint32_t page_size);

#endif // CLIENT_PROGRAM_H
