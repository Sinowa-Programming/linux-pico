#include "client_program.h"
#include <string.h>
#include <stdio.h>

FILE* open_client_binary(const char* filename) {
    if (!filename) {
        fprintf(stderr, "[CLIENT] Invalid filename\n");
        return NULL;
    }
    
    FILE* bin_file = fopen(filename, "rb");
    if (!bin_file) {
        fprintf(stderr, "[CLIENT] Failed to open binary file: %s\n", filename);
        return NULL;
    }
    
    // Get file size for validation
    fseek(bin_file, 0, SEEK_END);
    long file_size = ftell(bin_file);
    rewind(bin_file);
    
    if (file_size <= 0) {
        fprintf(stderr, "[CLIENT] Invalid file size for: %s\n", filename);
        fclose(bin_file);
        return NULL;
    }
    
    printf("[CLIENT] Opened binary file: %s (size: %ld bytes)\n", filename, file_size);
    return bin_file;
}

int32_t extract_page_from_binary(FILE* bin_file, uint32_t page_id, uint8_t* buffer, uint32_t page_size) {
    if (!bin_file || !buffer) {
        fprintf(stderr, "[CLIENT] Invalid parameters for extract_page_from_binary\n");
        return -1;
    }
    
    // Calculate file offset for this page
    long file_offset = (long)page_id * page_size;
    
    // Seek to the page location
    if (fseek(bin_file, file_offset, SEEK_SET) != 0) {
        fprintf(stderr, "[CLIENT] Failed to seek to page %u at offset %ld\n", page_id, file_offset);
        return -1;
    }
    
    // Clear the buffer
    memset(buffer, 0, page_size);
    
    // Read the page
    size_t bytes_read = fread(buffer, 1, page_size, bin_file);
    if (bytes_read > 0) {
        printf("[CLIENT] Extracted page %u with %zu bytes\n", page_id, bytes_read);
        return bytes_read;
    } else if (feof(bin_file)) {
        fprintf(stderr, "[CLIENT] Reached end of file while reading page %u\n", page_id);
        return -1;
    } else {
        fprintf(stderr, "[CLIENT] Error reading page %u from binary file\n", page_id);
        return -1;
    }
}
