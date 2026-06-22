#include <libusb-1.0/libusb.h>
#include <string.h> // For memcpy
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>   // For uint16_t printf

#include "usb_handler.h"
#include "file_functions.h"
#include "client_program.h"
#include "page_functions.h"

uint8_t sram_frames[NUM_PAGES][PAGE_SIZE];
uint32_t sizes_arr[NUM_PAGES] = {0};

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


static int32_t translate_to_page_index(uint32_t vaddr) {
    uint32_t offset = vaddr - VIRTUAL_MEMORY_BASE;
    uint32_t page_index = offset / PAGE_SIZE;
    if (page_index >= NUM_PAGES) return -1;
    return (int32_t)page_index;
}


// Convert internal page index to a client-facing virtual address
static uint32_t page_index_to_virtual(uint32_t page_index) {
    return (uint32_t)(VIRTUAL_MEMORY_BASE + (page_index * PAGE_SIZE));
}


// Helper function to log page data to a file and console
static void log_page_data(uint32_t page_id, const uint8_t *page_data, const char *operation) {
    char filename[64];
    // Create a clear filename, e.g., "page_0x20000000_WRITE.bin"
    snprintf(filename, sizeof(filename), "page_0x%08X_%s.bin", page_id, operation);
    
    // Save to binary file
    FILE *log_file = fopen(filename, "wb");
    if (log_file) {
        fwrite(page_data, 1, PAGE_SIZE, log_file);
        fclose(log_file);
        printf("[DEBUG] Saved full page data to %s\n", filename);
    } else {
        fprintf(stderr, "[Error] Failed to open %s for logging.\n", filename);
    }

    // Print a hex dump of the first 64 bytes to the console
    printf("[DEBUG] Hex dump (first 64 bytes) of page 0x%08X:\n", page_id);
    for (int i = 0; i < 64 && i < PAGE_SIZE; i++) {
        printf("%02X ", page_data[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");
}


void Start_Rp2350Host() {
    libusb_init(&ctx);
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
    int32_t start_page = allocate_pages(sram_bitmap, sizes_arr, pages_needed);
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

    printf("[INFO] Client Program command sent with address: 0x%X\n", starting_addr);

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
    printf("[DEBUG] Header CMD RAW: raw=0x%08X\n", header->cmd);
    switch(header->cmd) {
        case PAGE_TABLE_READ: {
            // Read 4 bytes for virtual address
            uint32_t page_id = 0;
            memcpy(&page_id, payload, sizeof(uint32_t));

            if (page_id >= 0) {
                printf("[CMD] Read Request for page_id=%u. Virtual Address: 0x%08X. Sending data...\n", page_id, page_index_to_virtual(page_id));
                
                CommunicationHeader tx_header = {
                    .mcu_id = header->mcu_id,
                    .cmd = PAGE_TABLE_WRITE,
                    .data_length = 0    // Ignored on the mcu( for now )
                };

                uint32_t total_tx_size = sizeof(CommunicationHeader) + PAGE_SIZE;
                uint8_t* tx_buffer = (uint8_t*)malloc(total_tx_size);
                
                memcpy(tx_buffer, &tx_header, sizeof(CommunicationHeader));
                memcpy(tx_buffer + sizeof(CommunicationHeader), sram_frames[page_id], PAGE_SIZE);
                
                int sent_bytes;
                int r = libusb_bulk_transfer(dev_handle, EP_OUT,
                                            tx_buffer, total_tx_size,
                                            &sent_bytes, 0);

                if (r != 0) {
                    fprintf(stderr, "[Error] Failed to send page data.\n");
                }
                printf("[INFO] Transmission Complete. Sent %d bytes\n", sent_bytes);

                free(tx_buffer); // Free to prevent memory leaks
            } else {
                fprintf(stderr, "[Error] Page id: %u out of bounds.\n", page_id);
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
                memcpy(sram_frames[pageIndex], payload + sizeof(uint32_t), PAGE_SIZE);

                printf("[CMD] Saved %u bytes to page (raw=0x%08X -> idx=%d).\n", PAGE_SIZE, pageIdRaw, pageIndex);

                const uint8_t *page_data = payload + sizeof(uint32_t);
                log_page_data(pageIdRaw, page_data, "WRITE_FROM_MCU");
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
            
            int32_t assigned_page_id = allocate_pages(sram_bitmap, sizes_arr, num_pages_needed);
            
            if (assigned_page_id == -1) {
                fprintf(stderr, "[Error] Memory too fragmented to allocate %u bytes.\n", requested_size);
            } else {
                printf("[CMD] Allocated %u bytes starting at page %d (virtual 0x%08X)\n", requested_size, assigned_page_id, page_index_to_virtual(assigned_page_id));

                // Send back the allocated virtual address to the client with header
                uint32_t assigned_virtual = page_index_to_virtual((uint32_t)assigned_page_id);
                
                CommunicationHeader tx_header = {
                    .mcu_id = header->mcu_id,
                    .cmd = PAGE_TABLE_ALLOC,
                    .data_length = 0    // Ignored on the mcu( for now )
                };

                uint32_t total_tx_size = sizeof(CommunicationHeader) + sizeof(assigned_virtual);
                uint8_t* tx_buffer = (uint8_t*)malloc(total_tx_size);
                
                memcpy(tx_buffer, &tx_header, sizeof(CommunicationHeader));
                memcpy(tx_buffer + sizeof(CommunicationHeader), &assigned_virtual, sizeof(assigned_virtual));

                int sent_bytes;
                int r = libusb_bulk_transfer(dev_handle, EP_OUT,
                                            tx_buffer, total_tx_size,
                                            &sent_bytes, 0);
                if (r != 0) {
                    fprintf(stderr, "[Error] Failed to send allocated page ID.\n");
                }
                
                free(tx_buffer);
            }
            break;
        }
        
        case PAGE_TABLE_FREE: {
            uint32_t address_to_free;
            memcpy(&address_to_free, payload, sizeof(uint32_t));

            if(address_to_free != 0) {
                free_pages(sram_bitmap, sizes_arr, address_to_free);
                printf("[INFO] Freed memory at virtual address 0x%08X\n", address_to_free);
            } else {
                fprintf(stderr, "[ERROR] Address to free invalid: 0x%08X\n", address_to_free);
            }
            break;
        }

        /* MISC */
        case LOG: {
            char *log_string = malloc(header->data_length + 1);
            memcpy(log_string, payload, header->data_length);
            log_string[header->data_length] = '\0';
            printf("[CLIENT LOG] %s\n", log_string);
            free(log_string);
            break;
        }

        /* FILE */
        case FILE_OPEN: {
            // Read filename string (null-terminated)
            char filename[MAX_FILENAME_LENGTH];
            uint32_t filename_len = (header->data_length < MAX_FILENAME_LENGTH) ? header->data_length : (MAX_FILENAME_LENGTH - 1);
            memcpy(filename, payload, filename_len);
            filename[filename_len] = '\0';
            
            int32_t file_id = open_file(filename);
            printf("[CMD] FILE_OPEN: %s\n", filename);
            
            CommunicationHeader tx_header = {
                .mcu_id = header->mcu_id,
                .cmd = FILE_OPEN,
                .data_length = sizeof(int32_t)
            };

            // Send back the file ID with header
            uint32_t total_tx_size = sizeof(CommunicationHeader) + sizeof(file_id);
            uint8_t* tx_buffer = (uint8_t*)malloc(total_tx_size);
            
            memcpy(tx_buffer, &tx_header, sizeof(CommunicationHeader));
            memcpy(tx_buffer + sizeof(CommunicationHeader), &file_id, sizeof(file_id));

            int sent_bytes;
            int r = libusb_bulk_transfer(dev_handle, EP_OUT, 
                                        tx_buffer, total_tx_size, 
                                        &sent_bytes, 0);
            if (r != 0) {
                fprintf(stderr, "[Error] Failed to send file ID.\n");
            }
            
            free(tx_buffer);
            break;
        }
        
        case FILE_CLOSE: {
            // Read the remote_file_id
            int32_t remote_file_id = 0;
            memcpy(&remote_file_id, payload, sizeof(int32_t));
            
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

            // Allocate one buffer to hold header + file data
            uint32_t max_tx_size = sizeof(CommunicationHeader) + file_read_header.data_length;
            uint8_t* tx_buffer = (uint8_t*)malloc(max_tx_size);
            
            if (!tx_buffer) {
                fprintf(stderr, "[Error] Memory allocation failed for file read.\n");
                break;
            }
            
            // Read file directly into tx_buffer (offset by the header size)
            int32_t bytes_read = read_file(file_read_header.remote_file_id, 
                                          file_read_header.file_offset, 
                                          file_read_header.data_length, 
                                          tx_buffer + sizeof(CommunicationHeader));
            
            if (bytes_read > 0) {
                CommunicationHeader tx_header = {
                    .mcu_id = header->mcu_id,
                    .cmd = FILE_READ,
                    .data_length = bytes_read    // Ignored on the mcu( for now )
                };
                
                // Prepend header to the successful file read
                memcpy(tx_buffer, &tx_header, sizeof(CommunicationHeader));
                uint32_t total_tx_size = sizeof(CommunicationHeader) + bytes_read;
                
                int sent_bytes;
                int r = libusb_bulk_transfer(dev_handle, EP_OUT, 
                                            tx_buffer, total_tx_size, 
                                            &sent_bytes, 0);
                if (r != 0) {
                    fprintf(stderr, "[Error] Failed to send file data.\n");
                }
            } else {    // Send a data length of zero to prevent a spinlock on the pico's side.
                CommunicationHeader tx_header = {
                    .mcu_id = header->mcu_id,
                    .cmd = FILE_READ,
                    .data_length = 0
                };
                int sent_bytes;
                libusb_bulk_transfer(dev_handle, EP_OUT, (unsigned char*)&tx_header, sizeof(tx_header), &sent_bytes, 0);
            }
            
            free(tx_buffer);
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
                      file_write_header.data_length,  // Write VIRTUAL_FILE_PAGE_SIZE bytes as sent by client
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
        
        printf("Header: {\nMCU_ID = \t%" PRIu8 ",\nCMD = \t%" PRIu8 ",\nSIZE = \t%" PRIu16 "}\n",
                    header.mcu_id, header.cmd, header.data_length);

        if (r == 0 && transferred == sizeof(header)) {
            // 2. Read the Payload (if data_length > 0)
            if (header.data_length > 0) {
                uint32_t expected = (uint32_t)header.data_length;
                uint8_t* payload = malloc(expected);
                if (!payload) {
                    fprintf(stderr, "[Error] Failed to allocate %u bytes for payload\n", expected);
                    continue;
                }

                uint32_t total_received = 0;
                while (total_received < expected) {
                    int to_read = (int)(expected - total_received);
                    r = libusb_bulk_transfer(dev_handle, EP_IN,
                                             payload + total_received,
                                             to_read,
                                             &transferred, 0); // 1s timeout per chunk

                    if (r == 0 && transferred > 0) {
                        total_received += (uint32_t)transferred;
                        continue;
                    }

                    if (r == LIBUSB_ERROR_TIMEOUT) {
                        fprintf(stderr, "[Error] Timeout while reading payload (%u/%u)\n", total_received, expected);
                        break;
                    }

                    if (r == LIBUSB_ERROR_NO_DEVICE) {
                        free(payload);
                        return; // device disconnected
                    }

                    fprintf(stderr, "[Error] libusb_bulk_transfer failed while reading payload: %s\n", libusb_error_name(r));
                    break;
                }

                if (total_received == expected) {
                    processPayload(&header, payload);
                } else {
                    fprintf(stderr, "[Error] Incomplete payload received. Expected %u, got %u\n", expected, total_received);
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