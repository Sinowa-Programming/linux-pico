#include "file_functions.h"
#include <string.h>
#include <stdio.h>

FileDescriptor open_files[MAX_OPEN_FILES];

int32_t open_file(const char* filename) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!open_files[i].is_open) {
            FILE* f = fopen(filename, "r+b");
            if (!f) {
                f = fopen(filename, "w+b");
            }
            
            if (f) {
                open_files[i].file_handle = f;
                open_files[i].remote_file_id = i;
                open_files[i].is_open = true;
                strncpy(open_files[i].filename, filename, MAX_FILENAME_LENGTH - 1);
                open_files[i].filename[MAX_FILENAME_LENGTH - 1] = '\0';
                printf("[FILE] Opened file: %s (ID: %d)\n", filename, i);
                return i;
            } else {
                fprintf(stderr, "[FILE] Failed to open file: %s\n", filename);
                return -1;
            }
        }
    }
    fprintf(stderr, "[FILE] Max open files reached.\n");
    return -1;
}

bool close_file(int32_t file_id) {
    if (file_id < 0 || file_id >= MAX_OPEN_FILES || !open_files[file_id].is_open) {
        fprintf(stderr, "[FILE] Invalid file ID: %d\n", file_id);
        return false;
    }
    
    if (fclose(open_files[file_id].file_handle) == 0) {
        open_files[file_id].is_open = false;
        printf("[FILE] Closed file: %s (ID: %d)\n", open_files[file_id].filename, file_id);
        return true;
    }
    
    fprintf(stderr, "[FILE] Failed to close file ID: %d\n", file_id);
    return false;
}

int32_t read_file(int32_t file_id, uint32_t offset, uint32_t length, uint8_t* buffer) {
    if (file_id < 0 || file_id >= MAX_OPEN_FILES || !open_files[file_id].is_open) {
        fprintf(stderr, "[FILE] Invalid file ID for read: %d\n", file_id);
        return -1;
    }
    
    if (fseek(open_files[file_id].file_handle, offset, SEEK_SET) != 0) {
        fprintf(stderr, "[FILE] Seek failed for file ID: %d\n", file_id);
        return -1;
    }
    
    size_t bytes_read = fread(buffer, 1, length, open_files[file_id].file_handle);
    printf("[FILE] Read %zu bytes from file ID %d at offset %u\n", bytes_read, file_id, offset);
    return bytes_read;
}

bool write_file(int32_t file_id, uint32_t offset, uint32_t length, const uint8_t* buffer) {
    if (file_id < 0 || file_id >= MAX_OPEN_FILES || !open_files[file_id].is_open) {
        fprintf(stderr, "[FILE] Invalid file ID for write: %d\n", file_id);
        return false;
    }
    
    if (fseek(open_files[file_id].file_handle, offset, SEEK_SET) != 0) {
        fprintf(stderr, "[FILE] Seek failed for file ID: %d\n", file_id);
        return false;
    }
    
    size_t bytes_written = fwrite(buffer, 1, length, open_files[file_id].file_handle);
    printf("[FILE] Wrote %zu bytes to file ID %d at offset %u\n", bytes_written, file_id, offset);
    return bytes_written == length;
}
