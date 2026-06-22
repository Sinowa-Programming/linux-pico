#include "usb_handler.h"
#include <stdio.h>
#include <unistd.h>

// RUN WITH SUDO ON LINUX.

// I got this from the elf.map file
// It is the .text.function_entry_point_name
/* For example (The address is 0x0000000020082628):
 .text.dav1dplay_main
                0x0000000020082628      0x56c ../ported_programs/dav1d_pico_interface/libClient_program.a(dav1dplay.c.o)
                0x0000000020082628                dav1dplay_main

so the entry point is:
#define CLIENT_PROGRAM_ENTRY_POINT 0x0000000020082628
*/
#define CLIENT_PROGRAM_ENTRY_POINT 0x0000000020082374

int main(int argc, char* argv[]) {
    Start_Rp2350Host();
    while (!connect()) {
        usleep(100000);
    };

    // Load the client program from the specified file
    const char* client_program_file = (argc > 1) ? argv[1] : "pico_vpx.bin";
    const uint32_t client_program_entry_point = (argc > 2) ? atoi(argv[2]) : CLIENT_PROGRAM_ENTRY_POINT;

    int32_t pages_loaded = load_client_program_to_page_table(client_program_file);
    if (pages_loaded > 0) {
        while(!start_client_program(client_program_entry_point));
        printf("[MAIN] Client program loaded successfully\n");
    } else {
        fprintf(stderr, "[MAIN] Failed to load client program\n");
    }
    runListener();

    Stop_Rp2350Host();
    return 0;
}