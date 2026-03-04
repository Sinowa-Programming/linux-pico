#ifndef COMM_COMMANDS_H
#define COMM_COMMANDS_H
#include <stdint.h>

/*
* Byte setup:
* 1 byte: MCU ID
* 1 byte: Command
* 2 bytes: Data length( Different for PAGE_TABLE_SET )
* N bytes: Data (optional and sent separately after the header)
*
* ALT( PAGE_TABLE_SET ):
* - instead of Data length, the two bytes are:
* - - 1 byte: Frame idx to write to.
* - - 1 byte: Unused
*/

/*
* Each page will be 4098 bytes in transmission:
* 2 bytes - Page Id
* 4096 bytes - Page data
*/

typedef struct __attribute__((packed)) {
    uint8_t mcu_id;
    uint8_t cmd;
    uint16_t data_length;
} CommunicationHeader;

enum {
    // Client commands( Commands for the program running though the PAL ).
    START_CLIENT = 0x1,
    HALT_CLIENT = 0X2,
    RESUME_CLIENT = 0X3,

    PAGE_TABLE_ALLOC = 0xD,
    PAGE_TABLE_READ = 0xE,  // swap -> local page
    PAGE_TABLE_WRITE = 0xF   // local page -> swap
};


#endif  // COMM_COMMANDS_H