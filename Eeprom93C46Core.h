#ifndef EEPROM93C46CORE_H
#define EEPROM93C46CORE_H

#include "mbed.h"

// Microwireプロトコル用のGPIOピン定義
#define EEP_DI      P2_7   // データ入力（MOSI）
#define EEP_DO      P2_6   // データ出力（MISO）
#define EEP_CLK     P2_4   // クロック
#define EEP_CS      P2_5   // チップセレクト
#define EEP_ORG     P2_3   // 16ビットモード選択

// SPI互換性のためのエイリアス
#define EEP_MOSI    EEP_DI
#define EEP_MISO    EEP_DO

// コマンド定義
#define CMD_READ    0x80
#define CMD_WRITE   0x40
#define CMD_EWEN    0x30
#define CMD_EWDS    0x00

#define EEPROM_MAX_ADDR 0x7F  // 7ビットアドレス
#define EEPROM_DATA_START_ADDR 0x10  // 0x00～0x0FはMACアドレス等予約

class Eeprom93C46Core {
public:
    static Eeprom93C46Core& getInstance();
    
    void write_enable();
    void write_disable();
    void write_word(uint8_t addr, uint16_t value);
    uint16_t read_word(uint8_t addr);
    
private:
    Eeprom93C46Core();
    ~Eeprom93C46Core() {}
    
    // Microwireプロトコル用のGPIO
    DigitalOut di;    // データ入力
    DigitalIn  do_;   // データ出力
    DigitalOut clk;   // クロック
    DigitalOut cs;    // チップセレクト
    DigitalOut org;   // 16ビットモード選択
    
    // Microwireプロトコル用の基本操作
    void clock_pulse();
    void send_bit(bool bit);
    bool read_bit();
    void send_byte(uint8_t data);
    uint8_t read_byte();
    void wait_write_complete();
    
    // シングルトンパターン
    Eeprom93C46Core(const Eeprom93C46Core&) = delete;
    Eeprom93C46Core& operator=(const Eeprom93C46Core&) = delete;
};

#endif // EEPROM93C46CORE_H 