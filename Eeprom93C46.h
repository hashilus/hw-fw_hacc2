#ifndef EEPROM93C46_H
#define EEPROM93C46_H

#include "mbed.h"
#include "Eeprom93C46Core.h"

class Eeprom93C46 {
public:
    Eeprom93C46() {}
    void write_enable() { Eeprom93C46Core::getInstance().write_enable(); }
    void write_disable() { Eeprom93C46Core::getInstance().write_disable(); }
    void wait_write_complete() { thread_sleep_for(5); }  // 書き込み完了まで5ms待つ
    void write_word(uint8_t addr, uint16_t value) { Eeprom93C46Core::getInstance().write_word(addr, value); }
    uint16_t read_word(uint8_t addr) { return Eeprom93C46Core::getInstance().read_word(addr); }
    
    bool write_data(uint8_t addr, const uint8_t* data, size_t len) {
        if (addr + len > EEPROM_MAX_ADDR) return false;
        
        write_enable();
        for (size_t i = 0; i < len; i += 2) {
            uint16_t value;
            if (i + 1 < len) {
                value = (data[i] << 8) | data[i + 1];
            } else {
                value = data[i] << 8;
            }
            write_word(addr + i/2, value);
            wait_write_complete();
        }
        write_disable();
        return true;
    }
    
    bool read_data(uint8_t addr, uint8_t* data, size_t len) {
        if (addr + len > EEPROM_MAX_ADDR) return false;
        
        for (size_t i = 0; i < len; i += 2) {
            uint16_t value = read_word(addr + i/2);
            if (i < len) data[i] = (value >> 8) & 0xFF;
            if (i + 1 < len) data[i + 1] = value & 0xFF;
        }
        return true;
    }
};

#endif 