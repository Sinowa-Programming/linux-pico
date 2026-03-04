#include "usb_handler.h"

int main() {
    Start_Rp2350Host();
    while (!connect());

    runListener();

    Stop_Rp2350Host();
    return 0;
}