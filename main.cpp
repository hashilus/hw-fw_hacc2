/* mbed Microcontroller Library
 * Copyright (c) 2025 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mbed.h"
#include "SSRDriver.h"
#include "RGBLEDDriver.h"
#include "WS2812Driver.h"
#include "UDPController.h"
#include "ConfigManager.h"
#include "PinNames.h"
#include "MacAddress93C46.h"
#include "SerialController.h"
#include "NetworkManager.h"
#include "lwip/apps/netbiosns.h"
#include "lwip/netif.h"
#include "lwip/ip4.h"
#include "rtos.h"
#include "lwip/init.h"
#include "lwip/opt.h"
#include "BufferedSerial.h"
#include "EthernetInterface.h"
#include <memory>  // std::unique_ptr用
#include "iodefine.h"

// SSRの数とRGB LEDの数
#define SSR_NUM_CHANNELS 4
#define RGB_LED_NUM 4

// スリープ時間の単位を定義（C++11以降の時間リテラル用）
using namespace std::chrono;

// シリアル通信
static BufferedSerial pc(USBTX, USBRX, 115200);

// オンボードLEDのピン番号は適宜修正してください
static DigitalOut led_r(LED1);  // 赤LED
static DigitalOut led_g(LED2);  // 緑LED
static DigitalOut led_b(LED3);  // 青LED

// システムステータスを表す列挙型
enum SystemStatus {
    STATUS_INITIALIZING,    // 初期化中（青色点滅）
    STATUS_READY,           // 正常動作中（緑色点灯）
    STATUS_ERROR,           // エラー状態（赤色点滅）
    STATUS_PACKET_RECEIVED, // パケット受信（紫色、一時的）
    STATUS_COMMAND_EXEC     // コマンド実行中（オレンジ色、一時的）
};

// 現在のシステムステータス
volatile SystemStatus current_status = STATUS_INITIALIZING;

// パケット受信/コマンド実行表示の一時的なタイマー
Timeout status_timeout;

// グローバル変数
static std::unique_ptr<ConfigManager> config_manager;
static std::unique_ptr<NetworkManager> network_manager;
static std::unique_ptr<UDPController> udp_controller;
static SSRDriver ssr;
static std::unique_ptr<RGBLEDDriver> rgb_led;
static std::unique_ptr<WS2812Driver> ws2812_driver;
static SerialController serial_controller(nullptr, &ssr, nullptr, pc);

// SSRの出力に合わせてLEDを更新するタイマー
Ticker led_color_updater;

// シリアル出力用のミューテックス
static Mutex serial_mutex;

// Network interface
NetworkInterface* network;

// シリアル出力の安全な実装
void safe_printf(const char* format, ...) {
    serial_mutex.lock();
    
    va_list args;
    va_start(args, format);
    int len = vsnprintf(nullptr, 0, format, args);
    va_end(args);
    
    if (len < 0) {
        serial_mutex.unlock();
        return;
    }
    
    char* buffer = new char[len + 1];
    if (!buffer) {
        serial_mutex.unlock();
        return;
    }
    
    va_start(args, format);
    vsnprintf(buffer, len + 1, format, args);
    va_end(args);
    
    // 出力を128バイトのチャンクに分割して送信
    const size_t chunk_size = 128;
    size_t remaining = len;
    size_t offset = 0;
    
    while (remaining > 0) {
        size_t to_send = (remaining > chunk_size) ? chunk_size : remaining;
        pc.write(buffer + offset, to_send);
        offset += to_send;
        remaining -= to_send;
        wait_us(1000);  // 1ms待機
    }
    
    // 改行を追加
    pc.write("\n", 1);
    
    delete[] buffer;
    serial_mutex.unlock();
}
        
// LED表示を更新する関数
void update_status_led(SystemStatus status) {
    current_status = status;
}

// 一時的なステータス表示を元に戻す
void reset_temp_status() {
    if (current_status == STATUS_PACKET_RECEIVED || current_status == STATUS_COMMAND_EXEC) {
        current_status = STATUS_READY;
    }
        }
        
// LEDステータス表示スレッド
void led_status_thread() {
    bool blink_state = false;
    int blink_counter = 0;
    
    while (true) {
        switch (current_status) {
            case STATUS_INITIALIZING:
                // 青色点滅
                led_r = 0;
                led_g = 0;
                led_b = blink_state;
    
                // 500msごとに点滅
                blink_counter++;
                if (blink_counter >= 5) {  // 50ms x 5 = 250ms
                    blink_state = !blink_state;
                    blink_counter = 0;
                }
                break;
                
            case STATUS_READY:
                // 緑色点灯
                led_r = 0;
                led_g = 1;
                led_b = 0;
                break;
                
            case STATUS_ERROR:
                // 赤色点滅
                led_r = blink_state;
                led_g = 0;
                led_b = 0;
        
                // 1秒ごとに点滅
                blink_counter++;
                if (blink_counter >= 10) {  // 50ms x 10 = 500ms
                    blink_state = !blink_state;
                    blink_counter = 0;
                }
                break;
                
            case STATUS_PACKET_RECEIVED:
                // 紫色点灯（赤+青）
                led_r = 1;
                led_g = 0;
                led_b = 1;
                break;
                
            case STATUS_COMMAND_EXEC:
                // オレンジ色点灯（赤+緑、青無し）
                led_r = 1;
                led_g = 1;
                led_b = 0;
                break;
        }
        
        wait_us(50000);  // 50ms待機
    }
}

// パケット受信時に呼び出すコールバック
void packet_received(const char* command) {
    update_status_led(STATUS_PACKET_RECEIVED);
    status_timeout.attach(reset_temp_status, 200ms);
}

// コマンド実行時に呼び出すコールバック
void command_executed(const char* command) {
    update_status_led(STATUS_COMMAND_EXEC);
    status_timeout.attach(reset_temp_status, 500ms);
}

// ログレベル定義
#define LOG_LEVEL_DEBUG 3
#define LOG_LEVEL_INFO  2
#define LOG_LEVEL_WARN  1
#define LOG_LEVEL_ERROR 0

// グローバルなデバッグレベル（初期値はDEBUG）
int debug_level = LOG_LEVEL_DEBUG;

// ログ出力関数
void log_printf(int level, const char* format, ...) {
    // シリアル出力を同期化
    serial_mutex.lock();
    
    va_list args;
    char buffer[256];
    
    // バッファサイズの計算
    va_start(args, format);
    int size = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    // バッファサイズチェック
    if (size < 0 || size >= static_cast<int>(sizeof(buffer))) {
        safe_printf("[ERROR] Buffer overflow in log_printf");
        serial_mutex.unlock();
        return;
    }
    
    // ログレベルに応じた文字列を取得
    const char* level_str;
    switch (level) {
        case LOG_LEVEL_ERROR:
            level_str = "ERROR";
            break;
        case LOG_LEVEL_WARN:
            level_str = "WARN";
            break;
        case LOG_LEVEL_INFO:
            level_str = "INFO";
            break;
        case LOG_LEVEL_DEBUG:
            level_str = "DEBUG";
            break;
        default:
            level_str = "UNKNOWN";
            break;
    }
    
    // ログレベルに応じたカラーコードを取得
    const char* color_code;
    switch (level) {
        case LOG_LEVEL_ERROR:
            color_code = "\033[31m";  // 赤
            break;
        case LOG_LEVEL_WARN:
            color_code = "\033[33m";  // 黄
            break;
        case LOG_LEVEL_INFO:
            color_code = "\033[37m";  // 白
            break;
        case LOG_LEVEL_DEBUG:
            color_code = "\033[36m";  // シアン
            break;
        default:
            color_code = "\033[0m";   // リセット
            break;
    }
    
    // カラーコード付きで出力
    safe_printf("[%s] %s%s\033[0m", level_str, color_code, buffer);
    
    serial_mutex.unlock();
}

void print_reset_reason() {
    // RZ/A1Hにはリセット要因レジスタが存在しないため、判定不可
    log_printf(LOG_LEVEL_INFO, "[RESET] Reset reason detection is not supported on RZ/A1H");
}

void init_network() {
    // Initialize network interface
    if (!network_manager->init()) {
        log_printf(LOG_LEVEL_ERROR, "Network interface initialization failed");
        return;
    }

    // ConfigManagerから設定を読み込んで適用
    if (config_manager) {
        // DHCP設定
        if (config_manager->isDHCPEnabled()) {
            if (network_manager->get_interface()) {
                network_manager->get_interface()->set_dhcp(true);
            }
        } else {
            // 静的IP設定
            uint32_t ip = config_manager->getIPAddressValue();
            uint32_t mask = config_manager->getNetmaskValue();
            uint32_t gw = config_manager->getGatewayValue();
            
            char ip_str[16];
            char mask_str[16];
            char gw_str[16];
            
            // IPアドレスを文字列に変換
            snprintf(ip_str, sizeof(ip_str), "%ld.%ld.%ld.%ld",
                ip & 0xFF,
                (ip >> 8) & 0xFF,
                (ip >> 16) & 0xFF,
                (ip >> 24) & 0xFF
            );
            
            // サブネットマスクを文字列に変換
            snprintf(mask_str, sizeof(mask_str), "%ld.%ld.%ld.%ld",
                mask & 0xFF,
                (mask >> 8) & 0xFF,
                (mask >> 16) & 0xFF,
                (mask >> 24) & 0xFF
            );
            
            // ゲートウェイを文字列に変換
            snprintf(gw_str, sizeof(gw_str), "%ld.%ld.%ld.%ld",
                gw & 0xFF,
                (gw >> 8) & 0xFF,
                (gw >> 16) & 0xFF,
                (gw >> 24) & 0xFF
            );
            
            // 静的IP設定を適用
            if (network_manager->get_interface()) {
                network_manager->get_interface()->set_network(
                    SocketAddress(ip_str),
                    SocketAddress(mask_str),
                    SocketAddress(gw_str)
                );
            }
        }
    }

    // ネットワークに接続
    if (!network_manager->connect()) {
        log_printf(LOG_LEVEL_ERROR, "Network connection failed");
        return;
    }

    // ネットワーク接続の確認
    NetworkInterface* interface = network_manager->get_interface();
    if (!interface) {
        log_printf(LOG_LEVEL_ERROR, "Network interface is not available");
        return;
    }

    // 接続状態の確認
    if (interface->get_connection_status() != NSAPI_STATUS_GLOBAL_UP) {
        log_printf(LOG_LEVEL_ERROR, "Network is not connected");
        return;
    }

    // Initialize NETBIOS
    #if LWIP_NETBIOS
    // NETBIOSの初期化
    netbiosns_init();
    
    // NETBIOS名の設定
    const char* netbios_name = config_manager->getNetBIOSName();
    if (netbios_name && strlen(netbios_name) > 0) {
        netbiosns_set_name(netbios_name);
        log_printf(LOG_LEVEL_INFO, "NETBIOS name set to: %s", netbios_name);
    } else {
        log_printf(LOG_LEVEL_WARN, "No NETBIOS name configured");
    }
    #else
    log_printf(LOG_LEVEL_WARN, "NETBIOS support is disabled");
    #endif

    // Initialize UDP controller
    if (!udp_controller) {
        log_printf(LOG_LEVEL_ERROR, "UDP controller is not initialized");
        return;
    }

    if (!udp_controller->init(interface)) {
        log_printf(LOG_LEVEL_ERROR, "UDP controller initialization failed");
        return;
    }

    log_printf(LOG_LEVEL_INFO, "Network initialization completed successfully");
}

// RZ_A1H用ウォッチドッグタイマー制御
// WDTレジスタ定義（iodefine.hに含まれているが、明示的に定義）
#define WDT_BASE        0xFCFE0000uL
#define WDT_WTCSR       (*(volatile uint16_t*)(WDT_BASE + 0x00))
#define WDT_WTCNT       (*(volatile uint16_t*)(WDT_BASE + 0x02))
#define WDT_WRCSR       (*(volatile uint16_t*)(WDT_BASE + 0x04))

// WTCSRビット定義
#define WTCSR_TME       0x20    // Timer enable
#define WTCSR_WTIT      0x40    // Watchdog timer interrupt
#define WTCSR_IOVF      0x80    // Interrupt overflow flag
#define WTCSR_CKS_MASK  0x0F    // Clock select mask

// WRCSRビット定義
#define WRCSR_RSTE      0x40    // Reset enable
#define WRCSR_WOVF      0x80    // Watchdog overflow flag

// ウォッチドッグタイマー初期化
void init_watchdog() {
    // ウォッチドッグタイマーを無効化
    WDT_WTCSR &= ~WTCSR_TME;
    
    // リセット機能を有効化（システム監視用）
    WDT_WRCSR |= WRCSR_RSTE;
    
    // カウンターをクリア
    WDT_WTCNT = 0;
    
    // オーバーフローフラグをクリア
    WDT_WTCSR &= ~WTCSR_IOVF;
    WDT_WRCSR &= ~WRCSR_WOVF;
    
    // クロック設定（PCLK/256、約6秒タイムアウト）
    WDT_WTCSR = (WDT_WTCSR & ~WTCSR_CKS_MASK) | 0x08;  // PCLK/256
    
    // ウォッチドッグタイマーを有効化
    WDT_WTCSR |= WTCSR_TME;
    
    log_printf(LOG_LEVEL_INFO, "Watchdog timer initialized with ~6s timeout");
}

// ウォッチドッグタイマーkick処理
void kick_watchdog() {
    // カウンターをクリア
    WDT_WTCNT = 0;
}

int main()
{
    print_reset_reason();
    
    // ウォッチドッグタイマー初期化（リセット要因確認後）
    init_watchdog();
    
    // シリアル通信の初期化
    pc.set_baud(115200);
    pc.set_format(8, BufferedSerial::None, 1);
    
    // システムステータスLEDの初期化
    led_r = 0;
    led_g = 0;
    led_b = 0;
    
    // ステータスLED表示スレッドの開始
    Thread led_thread(osPriorityNormal, 1024);
    led_thread.start(led_status_thread);
    
    // システムステータスを初期化中に設定
    update_status_led(STATUS_INITIALIZING);
    
    // Initialize configuration manager
    log_printf(LOG_LEVEL_INFO, "Initializing configuration manager...");
    config_manager = std::make_unique<ConfigManager>();
    kick_watchdog();  // ConfigManager初期化後にkick（設定読み込み完了後）
    
    // Initialize RGB LED driver with config manager
    log_printf(LOG_LEVEL_INFO, "Initializing RGB LED driver...");
    kick_watchdog();  // RGBLEDDriver初期化前にkick
    rgb_led = std::make_unique<RGBLEDDriver>(ssr, config_manager.get());
    kick_watchdog();  // RGBLEDDriver初期化後にkick
    
    // Initialize WS2812 driver
    log_printf(LOG_LEVEL_INFO, "Initializing WS2812 driver...");
    ws2812_driver = std::make_unique<WS2812Driver>();
    kick_watchdog();  // 初期化中にkick
    
    // Initialize network manager
    log_printf(LOG_LEVEL_INFO, "Initializing network manager...");
    network_manager = std::make_unique<NetworkManager>(config_manager.get());
    kick_watchdog();  // 初期化中にkick
    
    // Initialize UDP controller
    log_printf(LOG_LEVEL_INFO, "Initializing UDP controller...");
    udp_controller = std::make_unique<UDPController>(ssr, *rgb_led, *ws2812_driver, config_manager.get());
    kick_watchdog();  // 初期化中にkick
    
    // Update serial controller with config manager and drivers
    log_printf(LOG_LEVEL_INFO, "Configuring serial controller...");
    serial_controller.set_config_manager(config_manager.get());
    serial_controller.set_rgb_led_driver(rgb_led.get());
    kick_watchdog();  // 初期化中にkick
    
    // Load configuration from EEPROM (ConfigManagerのコンストラクタで既に読み込み済み)
    log_printf(LOG_LEVEL_DEBUG, "Configuration already loaded in ConfigManager constructor");
    kick_watchdog();  // 設定読み込み確認後にkick
    
    // MACアドレスはEEPROMから自動的に読み込まれ、mbed_mac_address関数で設定される
    log_printf(LOG_LEVEL_INFO, "Network Settings:");
    kick_watchdog();  // ネットワーク設定表示開始前にkick
    
    char mac[6];
    mbed_mac_address(mac);  // EEPROMから読み込まれたMACアドレスを取得
    log_printf(LOG_LEVEL_INFO, "- MAC: %02X:%02X:%02X:%02X:%02X:%02X", 
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    kick_watchdog();  // MACアドレス取得後にkick
    
    // Initialize network services
    log_printf(LOG_LEVEL_INFO, "Initializing network services...");
    kick_watchdog();  // ネットワーク初期化開始前にkick
    
    init_network();
    kick_watchdog();  // ネットワーク初期化後にkick
    
    // Display current network settings
    log_printf(LOG_LEVEL_INFO, "- DHCP: %s", config_manager->isDHCPEnabled() ? "Enabled" : "Disabled");
    kick_watchdog();  // DHCP設定表示後にkick
    
    log_printf(LOG_LEVEL_INFO, "- Current IP: %s", config_manager->getCurrentIPAddress());
    kick_watchdog();  // IP設定表示後にkick
    
    log_printf(LOG_LEVEL_INFO, "- Current Netmask: %s", config_manager->getCurrentNetmask());
    kick_watchdog();  // ネットマスク設定表示後にkick
    
    log_printf(LOG_LEVEL_INFO, "- Current Gateway: %s", config_manager->getCurrentGateway());
    kick_watchdog();  // ゲートウェイ設定表示後にkick
    
    log_printf(LOG_LEVEL_INFO, "- NETBIOS: %s", config_manager->getNetBIOSName());
    log_printf(LOG_LEVEL_INFO, "------------------------------------------");
    kick_watchdog();  // ネットワーク設定表示後にkick
    
    // Display SSR-LED link status
    log_printf(LOG_LEVEL_INFO, "SSR-LED Link Status:");
    kick_watchdog();  // SSR-LED設定表示開始前にkick
    
    if (config_manager->isSSRLinkEnabled()) {
        log_printf(LOG_LEVEL_INFO, "- Status: Enabled");
        log_printf(LOG_LEVEL_INFO, "- Transition Time: %d ms", config_manager->getSSRLinkTransitionTime());
    } else {
        log_printf(LOG_LEVEL_INFO, "- Status: Disabled");
    }
    log_printf(LOG_LEVEL_INFO, "------------------------------------------");
    kick_watchdog();  // SSR-LED設定表示後にkick
    
    // Display communication interfaces
    log_printf(LOG_LEVEL_INFO, "Communication Interfaces:");
    log_printf(LOG_LEVEL_INFO, "- UDP: Port %d", config_manager->getUDPPort());
    log_printf(LOG_LEVEL_INFO, "- Serial: 115200 bps, 8N1");
    log_printf(LOG_LEVEL_INFO, "------------------------------------------");
    kick_watchdog();  // 通信インターフェース表示後にkick
    
    // Display available commands
    log_printf(LOG_LEVEL_INFO, "UDP SOCKET Available Commands:");
    kick_watchdog();  // コマンド表示開始前にkick
    
    log_printf(LOG_LEVEL_INFO, "  set/ssr <num>,<value>  Set SSR output (0-100%%, ON/OFF)");
    log_printf(LOG_LEVEL_INFO, "  freq <num>,<hz>        Set PWM frequency (1-10Hz)");
    log_printf(LOG_LEVEL_INFO, "  get <num>              Get current settings");
    log_printf(LOG_LEVEL_INFO, "  rgb <num>,<r>,<g>,<b>  Set RGB LED color (0-255)");
    log_printf(LOG_LEVEL_INFO, "  rgbget <num>           Get RGB LED color");
    log_printf(LOG_LEVEL_INFO, "  ws2812 <sys>,<led>,<r>,<g>,<b>  Set WS2812 LED color");
    log_printf(LOG_LEVEL_INFO, "  ws2812get <sys>,<led>  Get WS2812 LED color");
    log_printf(LOG_LEVEL_INFO, "  ws2812sys <sys>,<r>,<g>,<b>  Set WS2812 system color");
    log_printf(LOG_LEVEL_INFO, "  ws2812off <sys>        Turn off WS2812 system");
    log_printf(LOG_LEVEL_INFO, "  mist <ms>              Mist control (0-10000ms)");
    log_printf(LOG_LEVEL_INFO, "  air <level>            Air control (0:OFF, 1:Low, 2:High)");
    log_printf(LOG_LEVEL_INFO, "  sofia                  Cute Sofia");
    log_printf(LOG_LEVEL_INFO, "  info                   Display device information");
    log_printf(LOG_LEVEL_INFO, "  config                 Display configuration");
    log_printf(LOG_LEVEL_INFO, "  config ssrlink <on/off> Set SSR-LED link");
    log_printf(LOG_LEVEL_INFO, "  config ssrlink status   Show SSR-LED link status");
    log_printf(LOG_LEVEL_INFO, "  config rgb0 <led_id> <r> <g> <b> Set LED 0%% color");
    log_printf(LOG_LEVEL_INFO, "  config rgb100 <led_id> <r> <g> <b> Set LED 100%% color");
    log_printf(LOG_LEVEL_INFO, "  config trans <ms>      Set transition time (100-10000ms)");
    log_printf(LOG_LEVEL_INFO, "  config t <ms>          Short form for transition time");
    log_printf(LOG_LEVEL_INFO, "  config save             Save configuration");
    log_printf(LOG_LEVEL_INFO, "  config load             Load configuration");
    log_printf(LOG_LEVEL_INFO, "------------------------------------------");
    kick_watchdog();  // コマンド表示後にkick
    
    log_printf(LOG_LEVEL_INFO, "System initialization completed");
    
    // Set UDP controller callbacks
    udp_controller->setPacketCallback(packet_received);
    udp_controller->setCommandCallback(command_executed);
    udp_controller->setConfigManager(config_manager.get());
    kick_watchdog();  // コールバック設定後にkick
    
    // Start command processing loop
    log_printf(LOG_LEVEL_INFO, "Starting command processing...");
    
    // Run UDP controller and serial controller in separate threads
    rtos::Thread udp_thread;
    rtos::Thread serial_thread;
    
    udp_thread.start([]() {
        udp_controller->run();
    });
    
    serial_thread.start([]() {
        serial_controller.run();
    });
    
    // システムステータスを準備完了に設定
    update_status_led(STATUS_READY);
    
    // ウォッチドッグkick用カウンター
    uint32_t watchdog_counter = 0;
    
    // ゼロクロス監視用カウンター
    uint32_t zerox_monitor_counter = 0;
    uint32_t last_zerox_count = 0;
    
    // デバッグ情報表示用カウンター
    uint32_t debug_monitor_counter = 0;
    uint32_t last_debug_power_freq = 0;
    uint32_t last_debug_on_time_us = 0;
    
    // Main thread handles LED updates
    while (true) {
        // SSR制御の内部状態を更新（ゼロクロス・PWM対応）
        ssr.updateControl();
        
        // ウォッチドッグタイマーを定期的にkick（100msごと）
        watchdog_counter++;
        if (watchdog_counter >= 10) {  // 10ms x 10 = 100ms
            kick_watchdog();
            watchdog_counter = 0;
        }
        
        // Add short wait time to reduce CPU usage
        wait_us(10000);  // 10ms wait
    }
    
    return 0;
}