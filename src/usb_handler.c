#include <libusb-1.0/libusb.h>
#include <string.h> // For memcpy
#include <errno.h>
#include <stdint.h>

#include "usb_handler.h"
#include <stdio.h>


// --- Constants & Memory ---
#define VIRTUAL_MEMORY_SIZE (40 * 1024 * 1024)
#define PAGE_SIZE 4096
#define NUM_PAGES (VIRTUAL_MEMORY_SIZE / PAGE_SIZE)
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

void Start_Rp2350Host() {
    libusb_init(&ctx);
    // Initialize sram_frames to 0 or test data if needed
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


void processPayload(const CommunicationHeader *header, const uint8_t *payload) {
    switch(header->cmd) {
        case PAGE_TABLE_READ: { // Return the requested page
            uint16_t pageId = payload[0] | (payload[1] << 8);

            if (pageId < NUM_PAGES) {
                printf("[CMD] Read Request for Page %u. Sending data...\n", pageId);
                
                // Write from memory array -> USB OUT
                int sent_bytes;
                int r = libusb_bulk_transfer(dev_handle, EP_OUT, 
                                            sram_frames[pageId], PAGE_SIZE, 
                                            &sent_bytes, 1000);
                
                if (r != 0) {
                    fprintf(stderr, "[Error] Failed to send page data.\n");
                }
            } else {
                fprintf(stderr, "[Error] Page ID %u out of bounds.\n", pageId);
            }
        }
        case PAGE_TABLE_WRITE: {    // Save the sent page
            uint16_t pageId = payload[0] | (payload[1] << 8);

            if (pageId < NUM_PAGES) {
                // Write data to memory array
                // payload[0-1] is ID, payload[2...4097] is data
                memcpy(sram_frames[pageId], &payload[2], PAGE_SIZE);
                
                printf("[CMD] Saved %u bytes to Page %u.\n", PAGE_SIZE, pageId);
            } else {
                fprintf(stderr, "[Error] Page ID %u out of bounds.\n", pageId);
            }
        }
        case PAGE_TABLE_ALLOC: {    // Return a page id for a block of memory large enough for the send data
            uint32_t requested_size;
            memcpy(&requested_size, &payload[0], 4);   // size_t on a 32 bit machine is 4 bytes
            printf("[CMD] Alloc request of size: %u\n", requested_size);

            int32_t assigned_page_id = allocate_pages(sram_bitmap, NUM_PAGES, requested_size);

            if(assigned_page_id == -1) {
                fprintf(stderr, "[Error] Memory too fragmented to allocate size: %u.\n", requested_size);
            }

            printf("Allocated %u to page number %d", requested_size, assigned_page_id);
        }

        default:
            printf("[Error] Unknown cmd length: %u\n",header->data_length);

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
                    fprintf(stderr, "[Error] Incomplete payload received.\n");
                }
            }
        } else {
            // Handle disconnect or errors
            if (r == LIBUSB_ERROR_NO_DEVICE) break;
        }
    }
}