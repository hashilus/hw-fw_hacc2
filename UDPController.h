#pragma once

#include "mbed.h"
#include "SSRDriver.h"
#include "RGBLEDDriver.h"
#include "WS2812Driver.h"
#include "ConfigManager.h"
#include "EthernetInterface.h"
#include "main.h"  // log_printfの定義を含む
#include "netsocket/NetworkInterface.h"
#include "netsocket/NetworkStack.h"
#include "netsocket/SocketAddress.h"
#include "netsocket/UDPSocket.h"
#include <string>
#include <vector>

// UDPポートのデフォルト値
#define UDP_PORT 5555

// バッファサイズの定義
#define MAX_BUFFER_SIZE 1024

// デバッグ設定
#define DEBUG_LEVEL 1
#define DEBUG_STATUS_INTERVAL 60  // ステータス表示の間隔（秒）

// バージョン情報
#define DEVICE_NAME "ACpowerController"
#define DEVICE_VERSION "Ver 1.0.0"

class UDPController {
public:
    UDPController(SSRDriver& ssr_driver, RGBLEDDriver& rgb_led_driver, WS2812Driver& ws2812_driver, ConfigManager* config_manager);
    ~UDPController();
    
    // 初期化と実行
    bool init(NetworkInterface* interface);
    bool run();
    void stop();  // スレッド停止用

    // コールバック設定
    void setPacketCallback(void (*callback)(const char*)) { _packet_callback = callback; }
    void setCommandCallback(void (*callback)(const char*)) { _command_callback = callback; }
    
    void setConfigManager(ConfigManager* config_manager) {
        _config_manager = config_manager;
    }
    
private:
    // スレッド関連
    Thread _thread;
    bool _running;
    void _thread_func();  // スレッドのメイン関数

    // コマンド処理
    void processCommand(const char* command, int length);
    void processSetCommand(const char* args);
    void processFreqCommand(const char* args);
    void processGetCommand(const char* args);
    void processRGBCommand(const char* args);
    void processRGBGetCommand(const char* args);
    void processWS2812Command(const char* args);
    void processWS2812GetCommand(const char* args);
    void processWS2812SysCommand(const char* args);
    void processWS2812OffCommand(const char* args);
    void processSofiaCommand();
    void processInfoCommand();
    void processMistCommand(const char* args);
    void processAirCommand(const char* args);
    void processZeroCrossCommand();
    void generateErrorResponse(const char* command);
    void sendResponse(const char* response);
    
    // コマンド解析
    bool parseSSRCommand(const char* command, int& num, int& value);
    bool parseRGBCommand(const char* command, int& num, int& r, int& g, int& b);
    bool parseMistCommand(const char* command, int& duration);
    bool parseAirCommand(const char* command, int& level);
    bool parseFreqCommand(const char* command, int& num, int& hz);
    bool parseGetCommand(const char* command, int& num);
    bool parseRGBGetCommand(const char* command, int& num);
    
    // ドライバー参照
    SSRDriver& _ssr_driver;
    RGBLEDDriver& _rgb_led_driver;
    WS2812Driver& _ws2812_driver;
    
    // 設定マネージャー
    ConfigManager* _config_manager;
    
    // コールバック関数
    void (*_packet_callback)(const char*);
    void (*_command_callback)(const char*);
    
    // UDP通信
    UDPSocket _socket;
    SocketAddress _remote_addr;
    char _recv_buffer[MAX_BUFFER_SIZE];
    char _send_buffer[MAX_BUFFER_SIZE];
    
    // ミスト制御用変数
    bool _mist_active;
    uint32_t _mist_start_time;
    uint32_t _mist_duration;
    
    NetworkInterface* _interface;
}; 