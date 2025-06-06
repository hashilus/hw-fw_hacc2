#pragma once

#include "mbed.h"
#include "drivers/BufferedSerial.h"
#include "ConfigManager.h"
#include "SSRDriver.h"
#include "RGBLEDDriver.h"
#include "main.h"
#include <cstring>
#include <vector>
#include <string>

// コマンドコールバックの型定義
typedef void (*CommandCallback)(const char* command);

// ログレベル定義
#define LOG_LEVEL_DEBUG 3
#define LOG_LEVEL_INFO  2
#define LOG_LEVEL_WARN  1
#define LOG_LEVEL_ERROR 0

#define MAX_BUFFER_SIZE 256
#define MAX_HISTORY_SIZE 10

/**
 * シリアルコントローラークラス
 * シリアル通信を使用してコマンドを受信し、SSRドライバとRGB LEDドライバを制御する
 */
class SerialController {
public:
    /**
     * コンストラクタ
     * @param config_manager 設定マネージャへのポインタ
     * @param ssr_driver SSRドライバへの参照
     * @param rgb_led_driver RGB LEDドライバへの参照
     * @param pc シリアル通信を行うためのBufferedSerialオブジェクトへの参照
     */
    SerialController(ConfigManager* config_manager, SSRDriver* ssr_driver, RGBLEDDriver* rgb_led_driver, BufferedSerial& pc);
    
    /**
     * デストラクタ
     */
    ~SerialController();
    
    /**
     * 初期化
     * @return 初期化に成功した場合はtrue、失敗した場合はfalseを返す
     */
    bool init();
    
    /**
     * コマンド処理のメインループ
     * このメソッドは無限ループで実行され、シリアル通信を受信して処理する
     * @return 処理が正常に終了した場合はtrue、それ以外の場合はfalseを返す
     */
    bool run();

    /**
     * コマンド実行時のコールバックを設定
     * @param callback コールバック関数
     */
    void set_command_callback(CommandCallback callback) {
        _command_callback = callback;
    }

    /**
     * 設定マネージャを設定
     * @param config_manager 設定マネージャへのポインタ
     */
    void set_config_manager(ConfigManager* config_manager) {
        _config_manager = config_manager;
    }

    /**
     * SSRドライバを設定
     * @param ssr_driver SSRドライバへのポインタ
     */
    void set_ssr_driver(SSRDriver* ssr_driver) {
        _ssr_driver = ssr_driver;
    }
    
    /**
     * RGB LEDドライバを設定
     * @param rgb_led_driver RGB LEDドライバへのポインタ
     */
    void set_rgb_led_driver(RGBLEDDriver* rgb_led_driver) {
        _rgb_led_driver = rgb_led_driver;
    }

private:
    BufferedSerial& _pc;
    ConfigManager* _config_manager;
    SSRDriver* _ssr_driver;
    RGBLEDDriver* _rgb_led_driver;
    char _recv_buffer[MAX_BUFFER_SIZE];
    int _recv_index;
    bool _command_complete;
    CommandCallback _command_callback;
    
    // コマンド履歴関連
    std::vector<std::string> _command_history;
    int _history_index;
    int _cursor_position;
    
    /**
     * シリアルからの入力を処理する
     */
    void processSerialInput();
    
    /**
     * コマンドを処理する
     * @param command 受信したコマンド文字列
     */
    void processCommand(const char* command);
    
    /**
     * ヘルプメッセージを表示する
     */
    void displayHelp();

    // コマンド処理メソッド
    void handleSetCommand(const char* command);
    void handleFreqCommand(const char* command);
    void handleGetCommand(const char* command);
    void handleRGBCommand(const char* command);
    void handleRGBGetCommand(const char* command);
    void handleInfoCommand();
    void handleConfigCommand(const char* command);
    void handleDebugCommand(const char* command);
    void handleRebootCommand();  // 再起動コマンドのハンドラ
    
    // コマンド履歴関連のメソッド
    void addToHistory(const std::string& command);
    void showHistoryCommand(int index);
    void moveCursor(int position);
    void clearLine();
    void redrawLine();
}; 