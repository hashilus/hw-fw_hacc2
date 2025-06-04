#include "Eeprom93C46Core.h"
#include "main.h"
#include "mbed.h"



Eeprom93C46Core& Eeprom93C46Core::getInstance() {
    static Eeprom93C46Core instance;
    return instance;
}

Eeprom93C46Core::Eeprom93C46Core()
    : di(EEP_DI), do_(EEP_DO), clk(EEP_CLK), cs(EEP_CS), org(EEP_ORG)
{
    // 初期状態設定
    cs = 0;     // チップセレクト非アクティブ（LOW）
    clk = 0;    // クロック初期状態
    di = 0;     // データ入力初期状態
    org = 1;    // 16ビットモード
    wait_us(1);
    
    // EEPROMの初期化確認
    log_printf(LOG_LEVEL_INFO, "Initializing EEPROM (16-bit mode)...");

    // 一時的なデータ書き込み
    // write_enable();
    // uint8_t temp_data[] = {0x40, 0x00, 0x10, 0x00, 0x00, 0x02, 0xCB, 0x01, 0x02, 0x05};
    // for (int i = 0; i < sizeof(temp_data); i += 2) {
    //     uint16_t value = (temp_data[i] << 8) | temp_data[i+1];
    //     write_word(i/2, value);
    // }
    // write_disable();
    // log_printf(LOG_LEVEL_INFO, "Temporary data written to EEPROM");
}

void Eeprom93C46Core::clock_pulse() {
    clk = 1;
    wait_us(1);  // クロックHIGH時間
    clk = 0;
    wait_us(1);  // クロックLOW時間
}

void Eeprom93C46Core::send_bit(bool bit) {
    di = bit;
    clock_pulse();
}

bool Eeprom93C46Core::read_bit() {
    clock_pulse();
    return do_.read();
}

void Eeprom93C46Core::send_byte(uint8_t data) {
    for (int i = 7; i >= 0; i--) {
        send_bit((data >> i) & 0x01);
    }
}

uint8_t Eeprom93C46Core::read_byte() {
    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        data = (data << 1) | read_bit();
    }
    return data;
}

void Eeprom93C46Core::write_enable() {
    cs = 1;     // チップセレクトアクティブ（HIGH）
    wait_us(1);
    send_byte(0x01);  // Start Bit
    send_byte(CMD_EWEN);  // EWENコマンド
    cs = 0;     // チップセレクト非アクティブ（LOW）
    wait_us(1);
    log_printf(LOG_LEVEL_DEBUG, "EEPROM write enabled (EWEN)");
}

void Eeprom93C46Core::write_disable() {
    cs = 1;     // チップセレクトアクティブ（HIGH）
    wait_us(1);
    send_byte(0x00);  // Start Bit
    send_byte(CMD_EWDS);  // EWDSコマンド
    cs = 0;     // チップセレクト非アクティブ（LOW）
    wait_us(1);
    log_printf(LOG_LEVEL_DEBUG, "EEPROM write disabled (EWDS)");
}

void Eeprom93C46Core::wait_write_complete() {
    // 書き込み完了まで最大5ms待つ
    thread_sleep_for(5);
}

void Eeprom93C46Core::write_word(uint8_t addr, uint16_t value) {
    if (addr > EEPROM_MAX_ADDR/2) {
        log_printf(LOG_LEVEL_ERROR, "EEPROM address out of range: 0x%02X", addr);
        return;
    }
    
    cs = 1;     // チップセレクトアクティブ（HIGH）
    wait_us(1);
    send_byte(0x01);  // Start Bit
    send_byte(CMD_WRITE | (addr & 0x3F));  // WRITEコマンド + アドレス（6ビット）
    send_byte(value & 0xFF);  // 下位バイト
    send_byte((value >> 8) & 0xFF);  // 上位バイト
    cs = 0;     // チップセレクト非アクティブ（LOW）
    wait_write_complete();
    
    // 書き込みの確認
    uint16_t verify = read_word(addr);
    if (verify != value) {
        log_printf(LOG_LEVEL_ERROR, "EEPROM write verification failed at addr 0x%02X: wrote 0x%04X, read 0x%04X", 
                  addr, value, verify);
    }
}

uint16_t Eeprom93C46Core::read_word(uint8_t addr) {
    if (addr > EEPROM_MAX_ADDR/2) {
        log_printf(LOG_LEVEL_ERROR, "EEPROM address out of range: 0x%02X", addr);
        return 0xFFFF;
    }

    cs = 1;     // チップセレクトアクティブ（HIGH）
    wait_us(1);
    send_byte(0x01);  // Start Bit
    send_byte(CMD_READ | (addr & 0x3F));  // READコマンド + アドレス（6ビット）
    uint16_t data = read_byte() | (read_byte() << 8);  // 16ビットデータ受信（リトルエンディアン）
    cs = 0;     // チップセレクト非アクティブ（LOW）
    wait_us(1);
    
    return data;
} 