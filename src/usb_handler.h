#ifndef USB_HANDLER_H
#define USB_HANDLER_H

#include <stdbool.h>

#include "comm_commands.h"
#include "page_functions.h"

void Start_Rp2350Host();
void Stop_Rp2350Host();

bool connect();
void runListener();


#endif // USB_HANDLER_H