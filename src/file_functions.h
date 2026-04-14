#ifndef FILE_FUNCTIONS_H
#define FILE_FUNCTIONS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define MAX_OPEN_FILES 16
#define MAX_FILENAME_LENGTH 256

typedef struct {
    FILE* file_handle;
    char filename[MAX_FILENAME_LENGTH];
    int32_t remote_file_id;  // Allocated page ID for this file
    bool is_open;
} FileDescriptor;

/**
 * @brief Open a file for reading and writing
 * @param filename The name of the file to open
 * @return File ID on success, -1 on failure
 */
int32_t open_file(const char* filename);

/**
 * @brief Close an open file
 * @param file_id The file ID to close
 * @return true on success, false on failure
 */
bool close_file(int32_t file_id);

/**
 * @brief Read data from an open file
 * @param file_id The file ID to read from
 * @param offset The file offset to read from
 * @param length The number of bytes to read
 * @param buffer The buffer to read into
 * @return Number of bytes read, -1 on error
 */
int32_t read_file(int32_t file_id, uint32_t offset, uint32_t length, uint8_t* buffer);

/**
 * @brief Write data to an open file
 * @param file_id The file ID to write to
 * @param offset The file offset to write at
 * @param length The number of bytes to write
 * @param buffer The buffer to write from
 * @return true on success, false on failure
 */
bool write_file(int32_t file_id, uint32_t offset, uint32_t length, const uint8_t* buffer);

#endif // FILE_FUNCTIONS_H
