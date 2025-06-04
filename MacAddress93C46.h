#ifndef MAC_ADDRESS_93C46_H
#define MAC_ADDRESS_93C46_H

#include "mbed.h"
#include "Eeprom93C46Core.h"

#define MAC_ADDRESS_START_ADDR 0x02

class MacAddress93C46 {
public:
    MacAddress93C46();
    char address[6];
};

extern "C" void mbed_mac_address(char *macAdr);

#endif 