#include <libusb-1.0/libusb.h>
#include <string.h> // For memcpy
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "usb_handler.h"
#include "file_functions.h"
#include "client_program.h"

// --- Constants & Memory ---
#define VIRTUAL_MEMORY_SIZE (40 * 1024 * 1024)
#define PAGE_SIZE 4096
#define NUM_PAGES (VIRTUAL_MEMORY_SIZE / PAGE_SIZE)
#define VIRTUAL_FILE_PAGE_SIZE 4096
// Constant for the base address assigned in the linker script
#define VIRTUAL_MEMORY_BASE 0x20082000


uint8_t sram_frames[NUM_PAGES][PAGE_SIZE];

/* === Memory Objects === */
uint32_t sram_bitmap[(NUM_PAGES + 31) / 32] = {0};


// --- USB Configuration ---
// Ensure these match your RP2350 TinyUSB descriptor
const uint16_t VENDOR_ID = 0xCAFE; 
const uint16_t PRODUCT_ID = 0x4010; 
const int INTERFACE_NUM = 0;
const int EP_OUT = 0x01; // Host -> Device
const int EP_IN = 0x81;  // Device -> Host

libusb_context *ctx = NULL;
libusb_device_handle *dev_handle = NULL;

// Helper: translate a device-supplied page identifier (could be a virtual address
// starting at VIRTUAL_MEMORY_BASE or a plain page index) into a zero-based
// page index into `sram_frames`.
static int32_t translate_to_page_index(uint32_t page_id_or_vaddr) {
    // If the client sent a virtual address, translate it to page index.
    if (page_id_or_vaddr >= VIRTUAL_MEMORY_BASE) {
        uint32_t offset = page_id_or_vaddr - VIRTUAL_MEMORY_BASE;
        uint32_t page_index = offset / PAGE_SIZE;
        if (page_index >= NUM_PAGES) return -1;
        return (int32_t)page_index;
    }

    // Otherwise assume it's already a page index
    if (page_id_or_vaddr >= NUM_PAGES) return -1;
    return (int32_t)page_id_or_vaddr;
}

// Helper: convert internal page index to a client-facing virtual address
static uint32_t page_index_to_virtual(uint32_t page_index) {
    return (uint32_t)(VIRTUAL_MEMORY_BASE + (page_index * PAGE_SIZE));
}

void Start_Rp2350Host() {
    libusb_init(&ctx);
    // Initialize sram_frames to 0
    memset(sram_frames, 0, sizeof(sram_frames));
}


void Stop_Rp2350Host() {
    if (dev_handle) {
        libusb_release_interface(dev_handle, INTERFACE_NUM);
        libusb_close(dev_handle);
    }
    libusb_exit(ctx);
}

bool connect() {
    dev_handle = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID);
    if (!dev_handle) {
        fprintf(stderr, "Device not found.\n");
        return false;
    }
    
    if (libusb_kernel_driver_active(dev_handle, INTERFACE_NUM)) {
        libusb_detach_kernel_driver(dev_handle, INTERFACE_NUM);
    }
    
    if (libusb_claim_interface(dev_handle, INTERFACE_NUM) < 0) {
        fprintf(stderr, "Cannot claim interface.\n");
        return false;
    }
    return true;
}


int32_t load_client_program_to_page_table(const char* filename) {
    if (!filename) {
        fprintf(stderr, "[USB] Invalid filename for client program\n");
        return -1;
    }
    
    // Open the binary file
    FILE* bin_file = open_client_binary(filename);
    if (!bin_file) {
        fprintf(stderr, "[USB] Failed to open client program file\n");
        return -1;
    }
    
    // Get file size
    fseek(bin_file, 0, SEEK_END);
    long file_size = ftell(bin_file);
    rewind(bin_file);
    
    if (file_size <= 0) {
        fprintf(stderr, "[USB] Invalid file size for client program\n");
        fclose(bin_file);
        return -1;
    }
    
    // Calculate number of pages needed
    uint32_t pages_needed = (file_size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    printf("[USB] Loading client program (%ld bytes, %u pages)...\n", file_size, pages_needed);
    
    // Allocate pages in the page table
    int32_t start_page = allocate_pages(sram_bitmap, NUM_PAGES, pages_needed);
    if (start_page == -1) {
        fprintf(stderr, "[USB] Failed to allocate %u pages for client program\n", pages_needed);
        fclose(bin_file);
        return -1;
    }
    
    printf("[USB] Allocated pages %d to %d\n", start_page, start_page + pages_needed - 1);
    
    // Load the binary file page by page
    uint32_t pages_loaded = 0;
    for (uint32_t i = 0; i < pages_needed; i++) {
        int32_t bytes_read = extract_page_from_binary(bin_file, i, sram_frames[start_page + i], PAGE_SIZE);
        if (bytes_read > 0) {
            pages_loaded++;
        } else if (feof(bin_file) && pages_loaded > 0) {
            // End of file reached, we're done
            printf("[USB] Reached end of file at page %u\n", i);
            break;
        } else {
            fprintf(stderr, "[USB] Failed to read page %u\n", i);
            fclose(bin_file);
            return -1;
        }
    }
    
    fclose(bin_file);
    
    printf("[USB] Client program loaded successfully: %u pages at starting page %d\n", pages_loaded, start_page);
    return pages_loaded;
}

bool start_client_program(uint32_t starting_addr)
{
    // Initialize the packet. This is a cool C99 feature.
    struct __attribute__((packed)) {
        uint8_t mcu_id;
        uint8_t cmd;
        uint16_t data_length;
        uint32_t starting_addr;
    } packet = {
        .mcu_id = 0,
        .cmd = START_CLIENT,
        .data_length = sizeof(uint32_t),
        .starting_addr = starting_addr
    };

    int sent_bytes_cnt;
    
    // Send the entire struct in one transfer
    int r = libusb_bulk_transfer(
        dev_handle, 
        EP_OUT,
        (unsigned char*)&packet,
        sizeof(packet),
        &sent_bytes_cnt, 
        1000
    );

    if (r != 0) {
        fprintf(stderr, "[Error] Failed to send start command: %s\n", 
                libusb_error_name(r));
        return false;
    } else if (sent_bytes_cnt != sizeof(packet)) {
        fprintf(stderr, "[Warning] Partial transfer: sent %d/%zu bytes\n", 
                sent_bytes_cnt, sizeof(packet));
        return false;
    }
    return true;
}

void processPayload(const CommunicationHeader *header, const uint8_t *payload) {
    printf("[DEBUG] Header CMD RAW: raw=0x%08X", header->cmd);
    switch(header->cmd) {
        case PAGE_TABLE_READ: {
            // Read 4 bytes for page identifier/virtual address
            uint32_t pageIdRaw = 0;
            memcpy(&pageIdRaw, payload, sizeof(uint32_t));

            int32_t pageIndex = translate_to_page_index(pageIdRaw);
            if (pageIndex >= 0) {
                printf("[CMD] Read Request for page (raw=0x%08X -> idx=%d). Sending data...\n", pageIdRaw, pageIndex);

                int sent_bytes;
                int r = libusb_bulk_transfer(dev_handle, EP_OUT,
                                            sram_frames[pageIndex], PAGE_SIZE,
                                            &sent_bytes, 1000);

                if (r != 0) {
                    fprintf(stderr, "[Error] Failed to send page data.\n");
                }
            } else {
                fprintf(stderr, "[Error] Page identifier 0x%08X out of bounds.\n", pageIdRaw);
            }
            break;
        }
        
        case PAGE_TABLE_WRITE: {
            // Read 4 bytes for page identifier/virtual address and PAGE_SIZE bytes for data
            uint32_t pageIdRaw = 0;
            memcpy(&pageIdRaw, payload, sizeof(uint32_t));

            int32_t pageIndex = translate_to_page_index(pageIdRaw);
            if (pageIndex >= 0) {
                // payload[0-3] is page id/raw address, payload[4...] is data
                memcpy(sram_frames[pageIndex], &payload[4], PAGE_SIZE);

                printf("[CMD] Saved %u bytes to page (raw=0x%08X -> idx=%d).\n", PAGE_SIZE, pageIdRaw, pageIndex);
            } else {
                fprintf(stderr, "[Error] Page identifier 0x%08X out of bounds.\n", pageIdRaw);
            }
            break;
        }
        
        case PAGE_TABLE_ALLOC: {
            // Read 4-byte size request
            uint32_t requested_size = 0;
            memcpy(&requested_size, payload, sizeof(uint32_t));
            
            // Convert size in bytes to number of pages needed
            uint32_t num_pages_needed = (requested_size + PAGE_SIZE - 1) / PAGE_SIZE;
            printf("[CMD] Alloc request of size: %u bytes (%u pages)\n", requested_size, num_pages_needed);
            
            int32_t assigned_page_id = allocate_pages(sram_bitmap, NUM_PAGES, num_pages_needed);
            
            if (assigned_page_id == -1) {
                fprintf(stderr, "[Error] Memory too fragmented to allocate %u bytes.\n", requested_size);
            } else {
                printf("[CMD] Allocated %u bytes starting at page %d (virtual 0x%08X)\n", requested_size, assigned_page_id, page_index_to_virtual(assigned_page_id));

                // Send back the allocated virtual address to the client
                uint32_t assigned_virtual = page_index_to_virtual((uint32_t)assigned_page_id);
                int sent_bytes;
                int r = libusb_bulk_transfer(dev_handle, EP_OUT,
                                            (unsigned char*)&assigned_virtual, sizeof(assigned_virtual),
                                            &sent_bytes, 1000);
                if (r != 0) {
                    fprintf(stderr, "[Error] Failed to send allocated page ID.\n");
                }
            }
            break;
        }
        
        case FILE_OPEN: {
            // Read filename string (null-terminated)
            char filename[MAX_FILENAME_LENGTH];
            uint32_t filename_len = (header->data_length < MAX_FILENAME_LENGTH) ? header->data_length : (MAX_FILENAME_LENGTH - 1);
            memcpy(filename, payload, filename_len);
            filename[filename_len] = '\0';
            
            int32_t file_id = open_file(filename);
            printf("[CMD] FILE_OPEN: %s\n", filename);
            
            // Send back the file ID
            int sent_bytes;
            int r = libusb_bulk_transfer(dev_handle, EP_OUT, 
                                        (unsigned char*)&file_id, sizeof(file_id), 
                                        &sent_bytes, 1000);
            if (r != 0) {
                fprintf(stderr, "[Error] Failed to send file ID.\n");
            }
            break;
        }
        
        case FILE_CLOSE: {
            // Read the remote_file_id
            uint32_t remote_file_id = 0;
            memcpy(&remote_file_id, payload, sizeof(uint32_t));
            
            printf("[CMD] FILE_CLOSE: File ID %u\n", remote_file_id);
            close_file(remote_file_id);
            break;
        }
        
        case FILE_READ: {
            // Read struct with file_offset, data_length, remote_file_id
            struct __attribute__((__packed__)) {
                uint32_t file_offset;
                uint32_t data_length;
                uint32_t remote_file_id;
            } file_read_header;
            
            memcpy(&file_read_header, payload, sizeof(file_read_header));
            
            printf("[CMD] FILE_READ: offset=%u, length=%u, file_id=%u\n", 
                   file_read_header.file_offset, file_read_header.data_length, file_read_header.remote_file_id);
            
            // Allocate temporary buffer for file read
            uint8_t* read_buffer = malloc(file_read_header.data_length);
            if (!read_buffer) {
                fprintf(stderr, "[Error] Memory allocation failed for file read.\n");
                break;
            }
            
            int32_t bytes_read = read_file(file_read_header.remote_file_id, 
                                          file_read_header.file_offset, 
                                          file_read_header.data_length, 
                                          read_buffer);
            
            if (bytes_read > 0) {
                // Send back the read data
                int sent_bytes;
                int r = libusb_bulk_transfer(dev_handle, EP_OUT, 
                                            read_buffer, bytes_read, 
                                            &sent_bytes, 5000);
                if (r != 0) {
                    fprintf(stderr, "[Error] Failed to send file data.\n");
                }
            }
            free(read_buffer);
            break;
        }
        
        case FILE_WRITE: {
            // Read struct with file_offset, data_length, remote_file_id, then file data
            struct __attribute__((__packed__)) {
                uint32_t file_offset;
                uint32_t data_length;
                uint32_t remote_file_id;
            } file_write_header;
            
            memcpy(&file_write_header, payload, sizeof(file_write_header));
            
            printf("[CMD] FILE_WRITE: offset=%u, length=%u, file_id=%u\n", 
                   file_write_header.file_offset, file_write_header.data_length, file_write_header.remote_file_id);
            
            // Get pointer to the actual file data (after the header)
            const uint8_t* file_data = payload + sizeof(file_write_header);
            
            write_file(file_write_header.remote_file_id, 
                      file_write_header.file_offset, 
                      VIRTUAL_FILE_PAGE_SIZE,  // Write VIRTUAL_FILE_PAGE_SIZE bytes as sent by client
                      file_data);
            break;
        }
        
        default:
            printf("[Error] Unknown command: 0x%02X\n", header->cmd);
    }
}


// Main processing loop
void runListener() {
    printf("Listening for RP2350 commands...\n");
    CommunicationHeader header;
    int transferred;
    int r;

    while (true) {
        // 1. Wait for Header (Blocking read on EP_IN)
        r = libusb_bulk_transfer(dev_handle, EP_IN, 
                                    (unsigned char*)&header, sizeof(header), 
                                    &transferred, 0); // 0 = Infinite timeout (wait forever)

        if (r == 0 && transferred == sizeof(header)) {
            
            // 2. Read the Payload (if data_length > 0)
            if (header.data_length > 0) {
                uint8_t* payload = malloc(header.data_length);
                
                r = libusb_bulk_transfer(dev_handle, EP_IN, 
                                            payload, header.data_length, 
                                            &transferred, 1000); // 1s timeout for body

                if (r == 0 && transferred == header.data_length) {
                    processPayload(&header, payload);
                } else {
                    fprintf(stderr, "[Error] Incomplete payload received. Expected %u, got %d\n", header.data_length, transferred);
                }
                free(payload);
            } else {
                // Commands with no payload
                processPayload(&header, NULL);
            }
        } else {
            // Handle disconnect or errors
            if (r == LIBUSB_ERROR_NO_DEVICE) break;
        }
    }
}