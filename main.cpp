/* mbed Microcontroller Library
 * Copyright (c) 2025 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mbed.h"
#include "SSRDriver.h"
#include "RGBLEDDriver.h"
#include "WS2812Driver.h"
#include "IdleAnimator.h"
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
#include "rtos/Thread.h"
#include "cmsis_os.h"
#include "lwip/init.h"
#include "lwip/opt.h"
#include "BufferedSerial.h"
#include "EthernetInterface.h"
#include <memory>  // std::unique_ptr用
#include <cstring> // strlen用
#include "iodefine.h"

// SSRの数とRGB LEDの数
#define SSR_NUM_CHANNELS 4
#define RGB_LED_NUM 4

// スリープ時間の単位を定義（C++11以降の時間リテラル用）
using namespace std::chrono;

// グローバル変数としてUDPスレッドを宣言
rtos::Thread* udp_thread = nullptr;

// シリアル通信
static BufferedSerial pc(USBTX, USBRX, 115200);

// オンボードLEDのピン番号は適宜修正してください
static DigitalOut led_r(LED1);  // 赤LED
static DigitalOut led_g(LED2);  // 緑LED
static DigitalOut led_b(LED3);  // 青LED

// システムステータスを表す列挙型
enum SystemStatus {
    STATUS_INITIALIZING,    // 初期化中（オレンジ色点灯）
    STATUS_READY,           // 正常動作中（緑色点灯）
    STATUS_ERROR,           // エラー状態（オレンジ色点滅）
    STATUS_PACKET_RECEIVED, // パケット受信（紫色、一時的）
    STATUS_COMMAND_EXEC,    // コマンド実行中（オレンジ色、一時的）
    STATUS_SSR_ACTIVE,      // SSR出力中（赤色点灯）
    STATUS_NETWORK_DOWN     // ネットワーク未接続（青色点滅）
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
static std::unique_ptr<IdleAnimator> idle_animator;
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
                // オレンジ色点灯（赤+緑、青無し）
                led_r = 1;
                led_g = 1;
                led_b = 0;
                break;
                
            case STATUS_READY:
                // 緑色点灯
                led_r = 0;
                led_g = 1;
                led_b = 0;
                break;
                
            case STATUS_ERROR:
                // オレンジ色点滅（赤+緑）
                led_r = blink_state;
                led_g = blink_state;
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
                
                    case STATUS_SSR_ACTIVE:
            // 赤色点灯
            led_r = 1;
            led_g = 0;
            led_b = 0;
            break;
            
        case STATUS_NETWORK_DOWN:
            // 青色点滅
            led_r = 0;
            led_g = 0;
            led_b = blink_state;
            // 1秒ごとに点滅
            blink_counter++;
            if (blink_counter >= 10) {  // 50ms x 10 = 500ms
                blink_state = !blink_state;
                blink_counter = 0;
            }
            break;
        }
        
        wait_us(50000);  // 50ms待機
    }
}

// パケット受信時に呼び出すコールバック
void packet_received(const char* command) {
    update_status_led(STATUS_PACKET_RECEIVED);
    status_timeout.attach(reset_temp_status, 200ms);
    if (idle_animator) idle_animator->notifyActivity();
}

// コマンド実行時に呼び出すコールバック
void command_executed(const char* command) {
    update_status_led(STATUS_COMMAND_EXEC);
    status_timeout.attach(reset_temp_status, 500ms);
    if (idle_animator) idle_animator->notifyActivity();
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
    char output_buffer[512];
    snprintf(output_buffer, sizeof(output_buffer), "[%s] %s%s\033[0m", level_str, color_code, buffer);
    
    // 出力を128バイトのチャンクに分割して送信
    const size_t chunk_size = 128;
    size_t len = strlen(output_buffer);
    size_t remaining = len;
    size_t offset = 0;
    
    while (remaining > 0) {
        size_t to_send = (remaining > chunk_size) ? chunk_size : remaining;
        pc.write(output_buffer + offset, to_send);
        offset += to_send;
        remaining -= to_send;
        wait_us(1000);  // 1ms待機
    }
    
    // 改行を追加
    pc.write("\n", 1);
    
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

    // ネットワーク接続はメインループ内で行うため、ここでは初期化のみ
    log_printf(LOG_LEVEL_INFO, "Network manager initialized - connection will be attempted in main loop");

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
    // 起動時の安定化待機（シリアルポート接続の確立を待つ）
    ThisThread::sleep_for(2000ms);  // 2秒待機に延長
    
    // 起動メッセージ
    log_printf(LOG_LEVEL_INFO, "=== HACC2 System Starting ===");
    log_printf(LOG_LEVEL_INFO, "Build: %s %s", __DATE__, __TIME__);
    log_printf(LOG_LEVEL_INFO, "Target: %s", MBED_STRINGIFY(TARGET_NAME));
    log_printf(LOG_LEVEL_INFO, "==========================================");
    
    // シリアル通信の準備完了確認
    log_printf(LOG_LEVEL_INFO, "Serial communication ready - starting system initialization");
    
    print_reset_reason();
    
    // ウォッチドッグタイマー初期化（リセット要因確認後）
    init_watchdog();
    
    // シリアル通信の初期化
    pc.set_baud(115200);
    pc.set_format(8, BufferedSerial::None, 1);
    
    // シリアル通信の安定化待機
    ThisThread::sleep_for(1000ms);  // 1秒待機に延長
    
    // シリアル通信初期化完了確認
    log_printf(LOG_LEVEL_INFO, "Serial communication initialized (115200 bps, 8N1)");
    
    // システムステータスLEDの初期化
    led_r = 0;
    led_g = 0;
    led_b = 0;
    
    // ステータスLED表示スレッドの開始
    rtos::Thread led_thread(osPriorityNormal, 1024);
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

    // IdleAnimator 初期化
    idle_animator = std::make_unique<IdleAnimator>(rgb_led.get());
    idle_animator->setIdleTimeout( std::chrono::milliseconds(10000) ); // 10秒無通信で開始
    idle_animator->setIntervalRange( std::chrono::milliseconds(800), std::chrono::milliseconds(3000) );
    idle_animator->setFadeDuration( std::chrono::milliseconds(600) );
    idle_animator->start();
    
    // 初期化処理の完了を待機
    ThisThread::sleep_for(100ms);
    
    // Initialize WS2812 driver
    log_printf(LOG_LEVEL_INFO, "Initializing WS2812 driver...");
    ws2812_driver = std::make_unique<WS2812Driver>();
    kick_watchdog();  // 初期化中にkick
    
    // 初期化処理の完了を待機
    ThisThread::sleep_for(100ms);
    
    // Load configuration from EEPROM first (ConfigManagerのコンストラクタで既に読み込み済み)
    log_printf(LOG_LEVEL_INFO, "Configuration loaded from EEPROM");
    kick_watchdog();  // 設定読み込み確認後にkick
    
    // Apply SSR PWM frequency from configuration
    log_printf(LOG_LEVEL_INFO, "Applying SSR PWM frequency from configuration...");
    kick_watchdog();  // SSR周波数適用開始前にkick
    
    for (int i = 1; i <= 4; i++) {
        int8_t saved_freq = config_manager->getSSRPWMFrequency(i);
        if (ssr.setPWMFrequency(i, saved_freq)) {
            if (saved_freq == -1) {
                log_printf(LOG_LEVEL_INFO, "- SSR%d PWM Frequency: -1 (設定変更無効) (applied)", i);
            } else {
                log_printf(LOG_LEVEL_INFO, "- SSR%d PWM Frequency: %d Hz (applied)", i, saved_freq);
            }
        } else {
            if (saved_freq == -1) {
                log_printf(LOG_LEVEL_WARN, "- SSR%d PWM Frequency: -1 (設定変更無効) (failed to apply, using default)", i);
            } else {
                log_printf(LOG_LEVEL_WARN, "- SSR%d PWM Frequency: %d Hz (failed to apply, using default)", i, saved_freq);
            }
        }
    }
    log_printf(LOG_LEVEL_INFO, "------------------------------------------");
    kick_watchdog();  // SSR周波数適用後にkick
    
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
        
        // 各LEDの設定値を出力
        for (int i = 1; i <= 4; i++) {
            RGBColorData color0 = config_manager->getSSRLinkColor0(i);
            RGBColorData color100 = config_manager->getSSRLinkColor100(i);
            log_printf(LOG_LEVEL_INFO, "- RGB LED %d 0%% color: R:%d G:%d B:%d", i, color0.r, color0.g, color0.b);
            log_printf(LOG_LEVEL_INFO, "- RGB LED %d 100%% color: R:%d G:%d B:%d", i, color100.r, color100.g, color100.b);
        }
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
    log_printf(LOG_LEVEL_INFO, "  freq <num>,<hz>        Set PWM frequency (-1-10Hz, -1=設定変更無効)");
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
    
    // Run serial controller in separate thread (immediately)
    rtos::Thread serial_thread;
    serial_thread.start([]() {
        serial_controller.run();
    });
    
    // UDP controller will be started after network connection is established
    bool udp_started = false;
    
    // ネットワーク接続監視用カウンター
    uint32_t network_monitor_counter = 0;
    bool last_network_status = false;
    
    // 初期ネットワーク接続状態を設定
    last_network_status = network_manager->isConnected();
    
    // 初期ネットワーク接続を試行（起動時の接続失敗に対応）
    if (!last_network_status) {
        log_printf(LOG_LEVEL_INFO, "Attempting initial network connection...");
        if (network_manager->connect()) {
            log_printf(LOG_LEVEL_INFO, "Initial network connection successful");
            last_network_status = true;
        } else {
            log_printf(LOG_LEVEL_WARN, "Initial network connection failed - will retry in main loop");
            last_network_status = false;
        }
    }
    
    // ネットワークが接続されている場合はUDPControllerを開始
    if (last_network_status && !udp_started) {
        log_printf(LOG_LEVEL_INFO, "Network connected, starting UDP controller...");
        
        // ネットワークインターフェースをUDPコントローラーに設定
        NetworkInterface* interface = network_manager->get_interface();
        if (interface) {
            udp_controller->init(interface);
            udp_thread = new rtos::Thread(); // 新しいスレッドオブジェクトを作成
            udp_thread->start([]() {
                udp_controller->run();
            });
            udp_started = true;
        } else {
            log_printf(LOG_LEVEL_ERROR, "Network interface not available for UDP controller");
        }
    }
    
    // システムステータスを準備完了に設定
    update_status_led(STATUS_READY);
    
    // ウォッチドッグkick用カウンター
    uint32_t watchdog_counter = 0;
    
    // ゼロクロス監視用カウンター
    uint32_t zerox_monitor_counter = 0;
    uint32_t last_zerox_count = 0;
    
    // デバッグ情報表示用カウンター
    uint32_t debug_monitor_counter = 0;
    float last_debug_power_freq = 0.0f;
    uint32_t last_debug_on_time_us = 0;
    
    // 電源周波数デバッグ表示用カウンター
    uint32_t power_freq_debug_counter = 0;
    
    // Main thread handles LED updates
    while (true) {
        // SSR制御の内部状態を更新（ゼロクロス・PWM対応）
        ssr.updateControl();
        
        // SSR出力状態を監視
        bool ssr_active = false;
        for (int i = 1; i <= 4; i++) {
            if (ssr.getDutyLevel(i) > 0) {
                ssr_active = true;
                break;
            }
        }
        
        // ネットワーク接続状態を監視（5秒ごと）
        network_monitor_counter++;
        if (network_monitor_counter >= 500) {  // 10ms x 500 = 5000ms = 5秒
            bool current_network_status = network_manager->isConnected();
            
            // ネットワーク接続状態が変化した場合、または再接続後の状態確認
            if (current_network_status != last_network_status || 
                (current_network_status && !udp_started)) {
                if (current_network_status) {
                    log_printf(LOG_LEVEL_INFO, "Network connection restored");
                    
                                            // ネットワーク接続が確立されたらUDPControllerを開始
                        if (!udp_started) {
                            log_printf(LOG_LEVEL_INFO, "Starting UDP controller...");
                            // 既存のスレッドが実行中の場合は停止を待つ
                            if (udp_thread && udp_thread->get_state() == rtos::Thread::Running) {
                                log_printf(LOG_LEVEL_WARN, "Waiting for existing UDP thread to stop...");
                                udp_thread->join();
                            }
                            log_printf(LOG_LEVEL_INFO, "Starting UDP thread...");
                            log_printf(LOG_LEVEL_INFO, "UDP thread state before start: %d", (int)udp_thread->get_state());
                            
                            // スレッドがDeleted状態の場合はスレッドを適切に停止
                            if (udp_thread->get_state() == rtos::Thread::Deleted) {
                                log_printf(LOG_LEVEL_WARN, "UDP thread is in Deleted state, attempting to join");
                                udp_thread->join();
                                // Deleted状態のスレッドは再利用できないため、新しいスレッドオブジェクトを作成
                                log_printf(LOG_LEVEL_WARN, "Creating new UDP thread object");
                                delete udp_thread;
                                udp_thread = new rtos::Thread();
                            }
                            
                            udp_thread->start([]() {
                                log_printf(LOG_LEVEL_INFO, "UDP thread lambda started");
                                log_printf(LOG_LEVEL_INFO, "About to call udp_controller->run()");
                                bool result = udp_controller->run();
                                log_printf(LOG_LEVEL_INFO, "UDP controller run() returned: %s", result ? "true" : "false");
                            });
                            udp_started = true;
                            log_printf(LOG_LEVEL_INFO, "UDP thread start() called, udp_started set to true");
                            log_printf(LOG_LEVEL_INFO, "UDP thread state after start: %d", (int)udp_thread->get_state());
                        }
                } else {
                    log_printf(LOG_LEVEL_WARN, "Network connection lost, stopping UDP controller...");
                    
                    // UDPControllerを停止
                    if (udp_started) {
                        udp_controller->stop();
                        if (udp_thread) {
                            log_printf(LOG_LEVEL_INFO, "Waiting for UDP thread to stop...");
                            udp_thread->join();
                            log_printf(LOG_LEVEL_INFO, "UDP thread stopped");
                        }
                        udp_started = false;
                        log_printf(LOG_LEVEL_INFO, "UDP controller stopped");
                    }
                    
                    log_printf(LOG_LEVEL_WARN, "Attempting network reconnection...");
                    
                    // 強制的にネットワークを切断
                    network_manager->disconnect();
                    ThisThread::sleep_for(2s);  // 切断完了を待機
                    
                    // 再接続を試行（複数回試行）
                    bool reconnection_success = false;
                    for (int retry = 0; retry < 3; retry++) {
                        log_printf(LOG_LEVEL_INFO, "Reconnection attempt %d/3", retry + 1);
                        if (network_manager->connect()) {
                            log_printf(LOG_LEVEL_INFO, "Network reconnection successful");
                            current_network_status = true;
                            reconnection_success = true;
                            break;
                        } else {
                            log_printf(LOG_LEVEL_WARN, "Reconnection attempt %d failed", retry + 1);
                            if (retry < 2) {
                                ThisThread::sleep_for(3s);  // 3秒待機してから再試行
                            }
                        }
                    }
                    
                    if (!reconnection_success) {
                        log_printf(LOG_LEVEL_ERROR, "Network reconnection failed after all attempts");
                        current_network_status = false;
                    } else {
                        // 再接続成功後にUDPControllerを再起動
                        if (!udp_started) {
                            log_printf(LOG_LEVEL_INFO, "Restarting UDP controller after reconnection...");
                            
                            // ネットワークインターフェースをUDPコントローラーに設定
                            NetworkInterface* interface = network_manager->get_interface();
                            if (interface) {
                                udp_controller->init(interface);
                                
                                // 既存のスレッドオブジェクトをクリーンアップ
                                if (udp_thread) {
                                    delete udp_thread;
                                    udp_thread = nullptr;
                                }
                                
                                // 新しいスレッドオブジェクトを作成
                                udp_thread = new rtos::Thread();
                                log_printf(LOG_LEVEL_INFO, "Created new UDP thread object");
                                
                                udp_thread->start([]() {
                                    log_printf(LOG_LEVEL_INFO, "UDP thread lambda started");
                                    log_printf(LOG_LEVEL_INFO, "About to call udp_controller->run()");
                                    bool result = udp_controller->run();
                                    log_printf(LOG_LEVEL_INFO, "UDP controller run() returned: %s", result ? "true" : "false");
                                });
                                udp_started = true;
                                log_printf(LOG_LEVEL_INFO, "UDP thread start() called, udp_started set to true");
                                log_printf(LOG_LEVEL_INFO, "UDP thread state after start: %d", (int)udp_thread->get_state());
                            } else {
                                log_printf(LOG_LEVEL_ERROR, "Network interface not available for UDP controller restart");
                            }
                        }
                    }
                }
                last_network_status = current_network_status;
            }
            
            network_monitor_counter = 0;
        }
        
        // ネットワーク接続状態に応じてステータスLEDを更新
        if (!network_manager->isConnected()) {
            update_status_led(STATUS_NETWORK_DOWN);
        } else if (ssr_active) {
            update_status_led(STATUS_SSR_ACTIVE);
        } else {
            // ネットワーク接続済みでSSRが出力していない場合は通常状態
            update_status_led(STATUS_READY);
        }
        
        // ウォッチドッグタイマーを定期的にkick（100msごと）
        watchdog_counter++;
        if (watchdog_counter >= 10) {  // 10ms x 10 = 100ms
            kick_watchdog();
            watchdog_counter = 0;
        }
        
        // // 電源周波数デバッグ出力（2秒ごと）
        // power_freq_debug_counter++;
        // if (power_freq_debug_counter >= 50) {  // 10ms x 200 = 2000ms = 2秒
        //     // ゼロクロス統計情報を取得して表示
        //     uint32_t zerox_count, zerox_interval;
        //     float zerox_frequency;
        //     ssr.getZeroCrossStats(zerox_count, zerox_interval, zerox_frequency);
        //     log_printf(LOG_LEVEL_DEBUG, "Zero-Cross Stats: Count=%lu, Interval=%lu us, Freq=%lu.%lu Hz", 
        //                zerox_count, zerox_interval, (uint32_t)zerox_frequency, (uint32_t)(zerox_frequency * 100) % 100);
            
        //     power_freq_debug_counter = 0;
        // }
        
        // Add short wait time to reduce CPU usage
        wait_us(10000);  // 10ms wait
    }
    
    return 0;
}