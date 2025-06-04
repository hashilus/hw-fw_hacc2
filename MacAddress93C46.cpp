#include "MacAddress93C46.h"
#include "main.h"  // log_printfとLOG_LEVEL_*の定義を含む

char mac_global[6];
static MacAddress93C46 mac_instance;  // グローバルインスタンスを作成

MacAddress93C46::MacAddress93C46() {
    Eeprom93C46Core& eep = Eeprom93C46Core::getInstance();
    log_printf(LOG_LEVEL_INFO, "Reading MAC address from EEPROM (16bit mode)...");
    uint8_t eep_mac[6];
    
    // 16ビットモードで3ワード読み込み（アドレス0x02から開始）
    for (int i = 0; i < 3; i++) {
        uint16_t word = eep.read_word(MAC_ADDRESS_START_ADDR+ i);  // アドレス0x02から開始
        eep_mac[i*2] = (word >> 8) & 0xFF;    // 上位バイト
        eep_mac[i*2+1] = word & 0xFF;         // 下位バイト
        log_printf(LOG_LEVEL_DEBUG, "EEPROM MAC[%d,%d]: 0x%02X,0x%02X (word: 0x%04X)", 
                  i*2, i*2+1, eep_mac[i*2], eep_mac[i*2+1], word);
    }
    
    // 全バイトが0x00または0xFFなら未設定とみなす
    bool all_zero = true, all_ff = true;
    for (int i = 0; i < 6; i++) {
        if (eep_mac[i] != 0x00) all_zero = false;
        if (eep_mac[i] != 0xFF) all_ff = false;
    }
    if (all_zero || all_ff) {
        // 未設定→初期値をセット
        mac_global[0] = 0xDE;
        mac_global[1] = 0xAD;
        mac_global[2] = 0xBE;
        mac_global[3] = 0xEF;
        mac_global[4] = 0xCA;
        mac_global[5] = 0xFE;
        log_printf(LOG_LEVEL_WARN, "Invalid MAC in EEPROM, using default: DE:AD:BE:EF:CA:FE");
    } else {
        // 正常値→そのままセット
        memcpy(mac_global, eep_mac, 6);
        log_printf(LOG_LEVEL_INFO, "Valid MAC read from EEPROM: %02X:%02X:%02X:%02X:%02X:%02X",
            mac_global[0], mac_global[1], mac_global[2], mac_global[3], mac_global[4], mac_global[5]);
    }
}

extern "C" void mbed_mac_address(char *macAdr) {
    memcpy(macAdr, mac_global, 6);
} 