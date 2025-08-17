#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "mbed.h"
#include "Eeprom93C46.h"
#include "ConfigData.h"
#include <string>

// 定数定義
#define CONFIG_VERSION 1
#define DEFAULT_UDP_PORT 5555
#define EEPROM_CONFIG_ADDR 8
#define DEFAULT_NETBIOS_NAME "HASHILUS-HACC"

/**
 * 設定管理クラス
 * JSON形式の設定ファイルを読み書きし、アプリケーション設定を管理する
 */
class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager();

    // 設定の保存と読み込み
    bool saveConfig();
    bool loadConfig(bool force = false);

    int getDebugLevel() const { return _data.debug_level; }
    void setDebugLevel(int level) { 
        if (level >= 0 && level <= 3) {
            _data.debug_level = level;
            saveConfig();  // 変更を即座に保存
        }
    }
    int getUDPPort() const { return _data.udp_port; }
    
    // ネットワーク設定
    void setDHCPMode(bool enabled);
    bool isDHCPEnabled() const { return _data.dhcp_enabled; }
    void setDHCPEnabled(bool enabled) { _data.dhcp_enabled = enabled; }
    bool setIPAddress(const char* ip);
    bool setNetmask(const char* netmask);
    bool setGateway(const char* gateway);
    const char* getIPAddress() const;
    const char* getNetmask() const;
    const char* getGateway() const;

    // 数値としての取得関数を追加
    uint32_t getIPAddressValue() const;
    uint32_t getNetmaskValue() const;
    uint32_t getGatewayValue() const;

    // NETBIOS名の設定と取得
    bool setNetBIOSName(const char* name);
    const char* getNetBIOSName() const;

    // LED-SSR連動設定
    void setSSRLink(bool enabled);
    bool isSSRLinkEnabled() const;
    void setSSRLinkTransitionTime(uint16_t ms);  // トランジション時間を設定（ミリ秒）
    uint16_t getSSRLinkTransitionTime() const;   // トランジション時間を取得（ミリ秒）
    void setSSRLinkColor0(int led_id, uint8_t r, uint8_t g, uint8_t b);  // 0%時の色を設定
    void setSSRLinkColor100(int led_id, uint8_t r, uint8_t g, uint8_t b);  // 100%時の色を設定
    RGBColorData getSSRLinkColor0(int led_id) const;  // 0%時の色を取得
    RGBColorData getSSRLinkColor100(int led_id) const;  // 100%時の色を取得
    RGBColorData calculateLEDColorForSSR(int led_id, int duty) const;  // デューティ比に応じた色を計算

    int8_t getSSRPWMFrequency(uint8_t channel = 0) const { 
        if (channel >= 1 && channel <= 4) {
            return _data.ssr_pwm_frequency[channel - 1]; 
        }
        return _data.ssr_pwm_frequency[0]; // デフォルトはチャンネル1
    }
    void setSSRPWMFrequency(uint8_t channel, int8_t freq, bool auto_save = true) { 
        if (channel >= 1 && channel <= 4) {
            _data.ssr_pwm_frequency[channel - 1] = freq; 
            if (auto_save) saveConfig(); 
        }
    }
    void setSSRPWMFrequency(int8_t freq, bool auto_save = true) { 
        // 全チャンネルに同じ周波数を設定
        for (int i = 0; i < 4; i++) {
            _data.ssr_pwm_frequency[i] = freq; 
        }
        if (auto_save) saveConfig(); 
    }

    void createDefaultConfig();

    bool usedDefaultConfig() const { return _used_default; }

    // コンフィグ情報の表示
    void printConfig() const;
    void printNetworkConfig() const;
    void printSSRLinkConfig() const;

    // 現在のネットワーク設定を取得
    const char* getCurrentIPAddress() const;
    const char* getCurrentNetmask() const;
    const char* getCurrentGateway() const;

private:
    ConfigData _data;
    Eeprom93C46 _eeprom;
    bool _used_default = false;
    static char _ip_buffer[16];  // IPアドレス文字列用バッファ
    static char _netmask_buffer[16];  // ネットマスク文字列用バッファ
    static char _gateway_buffer[16];  // ゲートウェイ文字列用バッファ

    // 設定ファイルのパス
    static const char* CONFIG_FILE;
    
    // バリデーション関数
    bool validateIPAddress(uint32_t ip) const;
    bool validateNetmask(uint32_t netmask) const;
    bool validateGateway(uint32_t gateway) const;
    bool validateNetBIOSName(const char* name) const;
};

#endif // CONFIG_MANAGER_H 