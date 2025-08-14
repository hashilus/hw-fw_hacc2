#include "ConfigManager.h"
#include <string.h>
#include <cstring>
#include <cstdio>
#include "SerialController.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"

// 静的バッファの初期化
char ConfigManager::_ip_buffer[16];
char ConfigManager::_netmask_buffer[16];
char ConfigManager::_gateway_buffer[16];

ConfigManager::ConfigManager() {
    // まずEEPROMから設定を読み込む
    if (!loadConfig(true)) {
        log_printf(LOG_LEVEL_INFO, "Creating default configuration...");
        createDefaultConfig();
    } else {
        log_printf(LOG_LEVEL_INFO, "Configuration loaded successfully from EEPROM");
    }
}

ConfigManager::~ConfigManager() {
}

bool ConfigManager::loadConfig(bool create_if_not_exist) {
    log_printf(LOG_LEVEL_DEBUG, "Loading configuration from EEPROM...");
    _used_default = false;
    
    // ワード単位（16ビット）でサイズを計算
    const size_t config_size_words = (sizeof(_data) + 1) / 2;  // バイト数を2で割って切り上げ
    log_printf(LOG_LEVEL_DEBUG, "Config size: %d bytes (%d words)", sizeof(_data), config_size_words);
    
    if (!_eeprom.read_data(EEPROM_CONFIG_ADDR, (uint8_t*)&_data, config_size_words * 2)) {
        log_printf(LOG_LEVEL_ERROR, "Failed to read from EEPROM");
        if (create_if_not_exist) {
            log_printf(LOG_LEVEL_INFO, "Creating default configuration...");
            createDefaultConfig();
            _used_default = true;
            return saveConfig();
        }
        return false;
    }
    log_printf(LOG_LEVEL_DEBUG, "Successfully read from EEPROM");

    // 全バイトが0または0xFFなら異常値とみなす
    bool all_zero = true, all_ff = true;
    const uint8_t* p = (const uint8_t*)&_data;
    for (size_t i = 0; i < sizeof(_data); i++) {
        if (p[i] != 0) all_zero = false;
        if (p[i] != 0xFF) all_ff = false;
    }
    log_printf(LOG_LEVEL_DEBUG, "Data check: all_zero=%d, all_ff=%d", all_zero, all_ff);

    // EEPROMのデータを表示
    log_printf(LOG_LEVEL_DEBUG, "EEPROM Data:");
    log_printf(LOG_LEVEL_DEBUG, "SSR-LED Link: %s", _data.ssr_link_enabled ? "Enabled" : "Disabled");
    log_printf(LOG_LEVEL_DEBUG, "Transition Time: %d ms", _data.ssr_link_transition_ms);
    
    for (int i = 0; i < 4; i++) {
        log_printf(LOG_LEVEL_DEBUG, "LED%d 0%%: R=%d G=%d B=%d", 
            i + 1,
            _data.ssr_link_colors_0[i].r,
            _data.ssr_link_colors_0[i].g,
            _data.ssr_link_colors_0[i].b);
            
        log_printf(LOG_LEVEL_DEBUG, "LED%d 100%%: R=%d G=%d B=%d", 
            i + 1,
            _data.ssr_link_colors_100[i].r,
            _data.ssr_link_colors_100[i].g,
            _data.ssr_link_colors_100[i].b);
    }

    // バージョン番号のチェック
    log_printf(LOG_LEVEL_DEBUG, "Checking version: current=%d, expected=%d", _data.version, CONFIG_VERSION);
    if (_data.version != CONFIG_VERSION) {
        log_printf(LOG_LEVEL_WARN, "Config version mismatch: expected %d, got %d", CONFIG_VERSION, _data.version);
        createDefaultConfig();
        _used_default = true;
        if (create_if_not_exist) {
            return saveConfig();
        }
        return false;
    }

    // ネットワーク設定の正当性チェック
    log_printf(LOG_LEVEL_DEBUG, "Validating network settings...");
    bool ip_valid = validateIPAddress(_data.ip_address);
    bool mask_valid = validateNetmask(_data.netmask);
    bool gw_valid = validateGateway(_data.gateway);
    log_printf(LOG_LEVEL_DEBUG, "Network validation: IP=%d, Mask=%d, Gateway=%d", 
               ip_valid, mask_valid, gw_valid);
    
    if (!ip_valid || !mask_valid || !gw_valid) {
        log_printf(LOG_LEVEL_WARN, "Invalid network settings detected");
        createDefaultConfig();
        _used_default = true;
        if (create_if_not_exist) {
            return saveConfig();
        }
        return false;
    }

    // UDPポートのバリデーション
    log_printf(LOG_LEVEL_DEBUG, "Checking UDP port: %d", _data.udp_port);
    if (_data.udp_port < 1024 || _data.udp_port > 65535) {
        log_printf(LOG_LEVEL_WARN, "Invalid UDP port: %d", _data.udp_port);
        createDefaultConfig();
        _used_default = true;
        if (create_if_not_exist) {
            return saveConfig();
        }
        return false;
    }

    // デバッグレベルのバリデーション
    log_printf(LOG_LEVEL_DEBUG, "Checking debug level: %d", _data.debug_level);
    if (_data.debug_level > 3) {
        log_printf(LOG_LEVEL_WARN, "Invalid debug level: %d", _data.debug_level);
        createDefaultConfig();
        _used_default = true;
        if (create_if_not_exist) {
            return saveConfig();
        }
        return false;
    }

    // NETBIOS名のバリデーション
    log_printf(LOG_LEVEL_DEBUG, "Validating NETBIOS name: %s", _data.netbios_name);
    if (!validateNetBIOSName(_data.netbios_name)) {
        log_printf(LOG_LEVEL_WARN, "Invalid NETBIOS name: %s", _data.netbios_name);
        createDefaultConfig();
        _used_default = true;
        if (create_if_not_exist) {
            return saveConfig();
        }
        return false;
    }

    log_printf(LOG_LEVEL_DEBUG, "Configuration validation completed successfully");
    return true;
}

bool ConfigManager::saveConfig() {
    // ワード単位（16ビット）でサイズを計算
    const size_t config_size_words = (sizeof(_data) + 1) / 2;  // バイト数を2で割って切り上げ
    return _eeprom.write_data(EEPROM_CONFIG_ADDR, (const uint8_t*)&_data, config_size_words * 2);
}

void ConfigManager::createDefaultConfig() {
    // デフォルト設定を初期化
    memset(&_data, 0, sizeof(ConfigData));
    
    // バージョン情報
    _data.version = CONFIG_VERSION;
    
    // ネットワーク設定
    _data.dhcp_enabled = true;
    _data.ip_address = htonl(0xC0A80164);     // 192.168.1.100
    _data.netmask = htonl(0xFFFFFF00);        // 255.255.255.0
    _data.gateway = htonl(0xC0A80101);        // 192.168.1.1
    
    // NETBIOS名
    strncpy(_data.netbios_name, DEFAULT_NETBIOS_NAME, sizeof(_data.netbios_name) - 1);
    _data.netbios_name[sizeof(_data.netbios_name) - 1] = '\0';
    
    // UDPポート
    _data.udp_port = DEFAULT_UDP_PORT;
    
    // デバッグレベル
    _data.debug_level = 1;  // デフォルトはINFOレベル
    
    // SSR-LED連動設定
    _data.ssr_link_enabled = true;  // デフォルトで有効化
    _data.ssr_link_transition_ms = 1000;  // デフォルトは1秒
    _data.ssr_pwm_frequency = 1; // SSR制御周波数デフォルト1Hz
    
    // 各LEDの色設定
    for (int i = 0; i < 4; i++) {
        // 0%時の色（青）
        _data.ssr_link_colors_0[i].r = 0;
        _data.ssr_link_colors_0[i].g = 0;
        _data.ssr_link_colors_0[i].b = 255;
        
        // 100%時の色（赤）
        _data.ssr_link_colors_100[i].r = 255;
        _data.ssr_link_colors_100[i].g = 0;
        _data.ssr_link_colors_100[i].b = 0;
    }
    
    // 設定を保存
    saveConfig();
}

void ConfigManager::setDHCPMode(bool enabled) {
    _data.dhcp_enabled = enabled;
}

bool ConfigManager::setIPAddress(const char* ip) {
    ip4_addr_t addr;
    if (ip4addr_aton(ip, &addr)) {
        _data.ip_address = addr.addr;
        return true;
    }
    return false;
}

bool ConfigManager::setNetmask(const char* netmask) {
    ip4_addr_t addr;
    if (ip4addr_aton(netmask, &addr)) {
        _data.netmask = addr.addr;
        return true;
    }
    return false;
}

bool ConfigManager::setGateway(const char* gateway) {
    ip4_addr_t addr;
    if (ip4addr_aton(gateway, &addr)) {
        _data.gateway = addr.addr;
        return true;
    }
    return false;
}

const char* ConfigManager::getIPAddress() const {
    ip4_addr_t addr;
    addr.addr = _data.ip_address;
    return ip4addr_ntoa(&addr);
}

const char* ConfigManager::getNetmask() const {
    ip4_addr_t addr;
    addr.addr = _data.netmask;
    return ip4addr_ntoa(&addr);
}

const char* ConfigManager::getGateway() const {
    ip4_addr_t addr;
    addr.addr = _data.gateway;
    return ip4addr_ntoa(&addr);
}

uint32_t ConfigManager::getIPAddressValue() const {
    return _data.ip_address;
}

uint32_t ConfigManager::getNetmaskValue() const {
    return _data.netmask;
}

uint32_t ConfigManager::getGatewayValue() const {
    return _data.gateway;
}

void ConfigManager::setSSRLink(bool enabled) {
    _data.ssr_link_enabled = enabled;
}

bool ConfigManager::isSSRLinkEnabled() const {
    return _data.ssr_link_enabled;
}

void ConfigManager::setSSRLinkColor0(int led_id, uint8_t r, uint8_t g, uint8_t b) {
    if (led_id >= 1 && led_id <= 4) {
        _data.ssr_link_colors_0[led_id-1].r = r;
        _data.ssr_link_colors_0[led_id-1].g = g;
        _data.ssr_link_colors_0[led_id-1].b = b;
    }
}

void ConfigManager::setSSRLinkColor100(int led_id, uint8_t r, uint8_t g, uint8_t b) {
    if (led_id >= 1 && led_id <= 4) {
        _data.ssr_link_colors_100[led_id-1].r = r;
        _data.ssr_link_colors_100[led_id-1].g = g;
        _data.ssr_link_colors_100[led_id-1].b = b;
    }
}

RGBColorData ConfigManager::getSSRLinkColor0(int led_id) const {
    if (led_id >= 1 && led_id <= 4) {
        return _data.ssr_link_colors_0[led_id-1];
    }
    return {0, 0, 0};
}

RGBColorData ConfigManager::getSSRLinkColor100(int led_id) const {
    if (led_id >= 1 && led_id <= 4) {
        return _data.ssr_link_colors_100[led_id-1];
    }
    return {0, 0, 0};
}

RGBColorData ConfigManager::calculateLEDColorForSSR(int led_id, int duty) const {
    if (!_data.ssr_link_enabled || led_id < 1 || led_id > 4) {
        return {0, 0, 0};  // 連動無効時または無効なLED IDは消灯
    }
    
    const RGBColorData& c0 = _data.ssr_link_colors_0[led_id-1];
    const RGBColorData& c1 = _data.ssr_link_colors_100[led_id-1];
    
            if (duty <= 0) return c0;
            if (duty >= 100) return c1;
    
    RGBColorData result;
            result.r = c0.r + (c1.r - c0.r) * duty / 100;
            result.g = c0.g + (c1.g - c0.g) * duty / 100;
            result.b = c0.b + (c1.b - c0.b) * duty / 100;
            return result;
        }

void ConfigManager::setSSRLinkTransitionTime(uint16_t ms) {
    _data.ssr_link_transition_ms = ms;
}

uint16_t ConfigManager::getSSRLinkTransitionTime() const {
    return _data.ssr_link_transition_ms;
}

bool ConfigManager::validateIPAddress(uint32_t ip) const {
    // 0.0.0.0と255.255.255.255は無効
    return ip != 0 && ip != 0xFFFFFFFF;
}

bool ConfigManager::validateNetmask(uint32_t netmask) const {
    // ネットマスクの形式チェック
    uint32_t mask = ntohl(netmask);
    uint32_t ones = 0;
    bool found_zero = false;
    
    for (int i = 31; i >= 0; i--) {
        if (mask & (1 << i)) {
            if (found_zero) return false;  // 1の後に0が来たら無効
            ones++;
        } else {
            found_zero = true;
        }
    }
    
    return ones > 0 && ones < 32;  // 少なくとも1ビットは1で、すべて1ではない
}

bool ConfigManager::validateGateway(uint32_t gateway) const {
    return validateIPAddress(gateway);
}

bool ConfigManager::setNetBIOSName(const char* name) {
    if (validateNetBIOSName(name)) {
        strncpy(_data.netbios_name, name, sizeof(_data.netbios_name) - 1);
        _data.netbios_name[sizeof(_data.netbios_name) - 1] = '\0';
        return true;
    }
    return false;
}

const char* ConfigManager::getNetBIOSName() const {
    return _data.netbios_name;
}

bool ConfigManager::validateNetBIOSName(const char* name) const {
    if (!name || strlen(name) == 0 || strlen(name) > 15) {
        return false;
    }
    
    // NETBIOS名の文字制限をチェック
    for (const char* p = name; *p; p++) {
        if (*p < 32 || *p > 126) {  // 印字可能なASCII文字のみ許可
            return false;
        }
    }
    
    return true;
}

void ConfigManager::printConfig() const {
    log_printf(LOG_LEVEL_INFO, "=== Configuration Information ===");
    log_printf(LOG_LEVEL_INFO, "Version: %d", _data.version);
    log_printf(LOG_LEVEL_INFO, "Debug Level: %d", _data.debug_level);
    
    printNetworkConfig();
    printSSRLinkConfig();
    log_printf(LOG_LEVEL_INFO, "SSR PWM Frequency: %d Hz", _data.ssr_pwm_frequency);
}

void ConfigManager::printNetworkConfig() const {
    log_printf(LOG_LEVEL_INFO, "=== Network Settings ===");
    log_printf(LOG_LEVEL_INFO, "DHCP: %s", _data.dhcp_enabled ? "Enabled" : "Disabled");
    log_printf(LOG_LEVEL_INFO, "IP Address: %s", getIPAddress());
    log_printf(LOG_LEVEL_INFO, "Subnet Mask: %s", getNetmask());
    log_printf(LOG_LEVEL_INFO, "Default Gateway: %s", getGateway());
    log_printf(LOG_LEVEL_INFO, "UDP Port: %d", _data.udp_port);
    log_printf(LOG_LEVEL_INFO, "NETBIOS Name: %s", _data.netbios_name);
}

void ConfigManager::printSSRLinkConfig() const {
    log_printf(LOG_LEVEL_INFO, "SSR-LED Link: %s", _data.ssr_link_enabled ? "Enabled" : "Disabled");
    log_printf(LOG_LEVEL_INFO, "Transition Time: %d ms", _data.ssr_link_transition_ms);
    log_printf(LOG_LEVEL_INFO, "SSR PWM Frequency: %d Hz", _data.ssr_pwm_frequency);
    for (int i = 0; i < 4; i++) {
        log_printf(LOG_LEVEL_INFO, "LED%d 0%%: R=%d G=%d B=%d", i + 1, _data.ssr_link_colors_0[i].r, _data.ssr_link_colors_0[i].g, _data.ssr_link_colors_0[i].b);
        log_printf(LOG_LEVEL_INFO, "LED%d 100%%: R=%d G=%d B=%d", i + 1, _data.ssr_link_colors_100[i].r, _data.ssr_link_colors_100[i].g, _data.ssr_link_colors_100[i].b);
    }
}

const char* ConfigManager::getCurrentIPAddress() const {
    struct netif *netif = netif_default;
    if (netif != NULL) {
        ip4addr_ntoa_r(&netif->ip_addr, _ip_buffer, sizeof(_ip_buffer));
        return _ip_buffer;
    }
    return "0.0.0.0";
}

const char* ConfigManager::getCurrentNetmask() const {
    struct netif *netif = netif_default;
    if (netif != NULL) {
        ip4addr_ntoa_r(&netif->netmask, _netmask_buffer, sizeof(_netmask_buffer));
        return _netmask_buffer;
    }
    return "0.0.0.0";
}

const char* ConfigManager::getCurrentGateway() const {
    struct netif *netif = netif_default;
    if (netif != NULL) {
        ip4addr_ntoa_r(&netif->gw, _gateway_buffer, sizeof(_gateway_buffer));
        return _gateway_buffer;
    }
    return "0.0.0.0";
} 