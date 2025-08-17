#ifndef CONFIG_DATA_H
#define CONFIG_DATA_H

#include <stdint.h>

struct RGBColorData {
    uint8_t r, g, b;
};

struct ConfigData {
    // 1バイトのメンバーをまとめる
    uint8_t version;                // 設定バージョン
    bool dhcp_enabled;              // DHCP有効/無効
    uint8_t debug_level;            // デバッグレベル
    bool ssr_link_enabled;          // SSR-LED連動有効/無効
    int8_t ssr_pwm_frequency[4];    // SSR制御周波数（Hz）、各チャンネル毎（-1〜10、-1は設定変更無効）

    // 2バイトのメンバーをまとめる
    uint16_t udp_port;              // UDPポート番号
    uint16_t ssr_link_transition_ms;// 色変化の時間（ミリ秒）

    // 4バイトのメンバーをまとめる
    uint32_t ip_address;            // IPアドレス（ネットワークバイトオーダー）
    uint32_t netmask;               // サブネットマスク（ネットワークバイトオーダー）
    uint32_t gateway;               // デフォルトゲートウェイ（ネットワークバイトオーダー）

    // 3バイトのメンバーをまとめる
    RGBColorData ssr_link_colors_0[4];  // 0%時の色（各LED, 1-4）
    RGBColorData ssr_link_colors_100[4];// 100%時の色（各LED, 1-4）

    // 16バイトのメンバー
    char netbios_name[16];          // NETBIOS名（最大15文字 + 終端文字）
};

#endif 