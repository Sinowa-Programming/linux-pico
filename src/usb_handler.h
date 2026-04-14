#ifndef USB_HANDLER_H
#define USB_HANDLER_H

#include <stdbool.h>

#include "comm_commands.h"
#include "page_functions.h"

void Start_Rp2350Host();
void Stop_Rp2350Host();

bool connect();
void runListener();

/**
 * @brief Load a binary client program into the page table
 * @param filename Path to the binary client program file
 * @return Number of pages loaded, -1 on failure
 */
int32_t load_client_program_to_page_table(const char* filename);

/**
 * @brief Transmits the address of the starting page
 * to the client to start it.
 * @param starting_addr The address the client is told to start at.
 */
bool start_client_program(uint32_t starting_addr);

#endif // USB_HANDLER_H