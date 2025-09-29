#include "UDPController.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "main.h"
#include "version.h"
#include "WS2812Driver.h"
#include "netsocket/NetworkInterface.h"
#include "netsocket/NetworkStack.h"
#include "netsocket/SocketAddress.h"
#include "netsocket/UDPSocket.h"
#include "netsocket/InternetSocket.h"
#include "mbed-os/connectivity/lwipstack/lwip/src/include/lwip/inet.h"
#include "mbed-os/connectivity/lwipstack/lwip/src/include/lwip/ip4_addr.h"

UDPController::UDPController(SSRDriver& ssr_driver, RGBLEDDriver& rgb_led_driver, WS2812Driver& ws2812_driver, ConfigManager* config_manager)
    : _ssr_driver(ssr_driver), _rgb_led_driver(rgb_led_driver), _ws2812_driver(ws2812_driver),
      _packet_callback(nullptr), _command_callback(nullptr),
      _config_manager(config_manager), _thread(nullptr), _mist_active(false),
      _mist_start_time(0), _mist_duration(0), _interface(nullptr),
      _running(false) {
    
    // Initialize buffers
    memset(_recv_buffer, 0, MAX_BUFFER_SIZE);
    memset(_send_buffer, 0, MAX_BUFFER_SIZE);
}

UDPController::~UDPController() {
    stop();
    _socket.close();
}

void UDPController::stop() {
    log_printf(LOG_LEVEL_INFO, "UDPController::stop() called");
    _running = false;
    
    // ソケットを閉じる
    _socket.close();
    log_printf(LOG_LEVEL_INFO, "UDP socket closed");
    
    if (_thread && _thread->get_state() == rtos::Thread::Running) {
        log_printf(LOG_LEVEL_INFO, "Waiting for UDP thread to stop...");
        _thread->join();
        log_printf(LOG_LEVEL_INFO, "UDP thread stopped");
    }
}

bool UDPController::init(NetworkInterface* interface) {
    if (!interface) {
        log_printf(LOG_LEVEL_ERROR, "Network interface is not initialized");
        return false;
    }

    _interface = interface;
    
    // 既存のソケットがあれば閉じる
    _socket.close();

    // ネットワーク接続状態を確認
    log_printf(LOG_LEVEL_INFO, "Checking network connection status...");
    nsapi_connection_status_t status = _interface->get_connection_status();
    if (status != NSAPI_STATUS_GLOBAL_UP) {
        log_printf(LOG_LEVEL_ERROR, "Network is not connected (status: %d), waiting for connection...", status);
        return false;
    }
    log_printf(LOG_LEVEL_INFO, "Network is connected (status: %d)", status);

    // Get UDP port from config manager
    int udp_port = UDP_PORT;  // デフォルト値
    if (_config_manager) {
        udp_port = _config_manager->getUDPPort();
        log_printf(LOG_LEVEL_DEBUG, "UDP port from config: %d", udp_port);
    }
    log_printf(LOG_LEVEL_INFO, "Initializing UDP socket on port %d...", udp_port);

    // Get local address for logging
    SocketAddress local_addr;
    if (_interface->get_ip_address(&local_addr) != 0) {
        log_printf(LOG_LEVEL_ERROR, "Failed to get local IP address");
        return false;
    }
    log_printf(LOG_LEVEL_INFO, "Local IP address: %s", local_addr.get_ip_address());

    // Open UDP socket
    if (_socket.open(_interface) != 0) {
        log_printf(LOG_LEVEL_ERROR, "Error UDP Socket Open");
        return false;
    }
    log_printf(LOG_LEVEL_INFO, "UDP socket opened successfully");

    // Set socket buffer sizes (reduced for compatibility)
    const int RECV_BUFFER_SIZE = 1024;  // 1KB
    const int SEND_BUFFER_SIZE = 1024;  // 1KB
    
    // Try to set socket buffer sizes (optional - will use default if fails)
    if (_socket.setsockopt(NSAPI_SOCKET, NSAPI_RCVBUF, &RECV_BUFFER_SIZE, sizeof(RECV_BUFFER_SIZE)) != 0) {
        log_printf(LOG_LEVEL_DEBUG, "Failed to set receive buffer size - using default");
    } else {
        log_printf(LOG_LEVEL_DEBUG, "Receive buffer size set to %d bytes", RECV_BUFFER_SIZE);
    }
    
    if (_socket.setsockopt(NSAPI_SOCKET, NSAPI_SNDBUF, &SEND_BUFFER_SIZE, sizeof(SEND_BUFFER_SIZE)) != 0) {
        log_printf(LOG_LEVEL_DEBUG, "Failed to set send buffer size - using default");
    } else {
        log_printf(LOG_LEVEL_DEBUG, "Send buffer size set to %d bytes", SEND_BUFFER_SIZE);
    }

    // Set socket timeout to 500ms for better reliability
    _socket.set_timeout(500);
    log_printf(LOG_LEVEL_INFO, "Socket timeout set to 500ms");

    // Bind port
    if (_socket.bind(udp_port) != 0) {
        log_printf(LOG_LEVEL_ERROR, "Error bind");
        _socket.close();
        return false;
    }
    log_printf(LOG_LEVEL_INFO, "Port bind successful");

    // Verify socket is ready
    if (_socket.recvfrom(NULL, NULL, 0) == NSAPI_ERROR_NO_SOCKET) {
        log_printf(LOG_LEVEL_ERROR, "Socket verification failed");
        _socket.close();
        return false;
    }
    
    log_printf(LOG_LEVEL_INFO, "UDP socket initialization complete");
    log_printf(LOG_LEVEL_INFO, "Listening on %s:%d", local_addr.get_ip_address(), udp_port);
    
    return true;
}

bool UDPController::run() {
    log_printf(LOG_LEVEL_INFO, "UDPController::run() called");
    
    // 既存のスレッドが実行中の場合は停止を待つ
    if (_thread && _thread->get_state() == rtos::Thread::Running) {
        log_printf(LOG_LEVEL_WARN, "Waiting for existing UDP thread to stop...");
        _running = false;  // 停止フラグを設定
        _thread->join();
        log_printf(LOG_LEVEL_INFO, "Existing UDP thread stopped");
    }
    
    // スレッドがDeleted状態または存在しない場合は新しいスレッドを作成
    if (!_thread || _thread->get_state() == rtos::Thread::Deleted) {
        log_printf(LOG_LEVEL_WARN, "UDP thread is in Deleted state, creating new thread");
        // 新しいスレッドオブジェクトを作成
        _thread = std::make_unique<rtos::Thread>();
    }

    // UDPソケットを初期化
    if (!_interface) {
        log_printf(LOG_LEVEL_ERROR, "Network interface is not available for UDP initialization");
        return false;
    }
    
    log_printf(LOG_LEVEL_INFO, "Initializing UDP socket before starting thread...");
    if (!init(_interface)) {
        log_printf(LOG_LEVEL_ERROR, "Failed to initialize UDP socket");
        return false;
    }

    log_printf(LOG_LEVEL_INFO, "Setting _running = true and starting thread...");
    _running = true;
    log_printf(LOG_LEVEL_INFO, "About to call _thread.start()...");
    _thread->start(callback(this, &UDPController::_thread_func));
    log_printf(LOG_LEVEL_INFO, "_thread.start() completed");
    log_printf(LOG_LEVEL_INFO, "UDP thread started");
    return true;
}

void UDPController::_thread_func() {
    log_printf(LOG_LEVEL_INFO, "UDP thread started");
    
    // ネットワーク接続を待つ
    log_printf(LOG_LEVEL_INFO, "Waiting for network connection...");
    while (_running && _interface->get_connection_status() != NSAPI_STATUS_GLOBAL_UP) {
        ThisThread::sleep_for(1s);
    }
    
    if (!_running) {
        log_printf(LOG_LEVEL_INFO, "UDP thread stopped while waiting for network");
        return;
    }
    
    log_printf(LOG_LEVEL_INFO, "Network connected, checking UDP socket status...");
    
    // UDPソケットが既に初期化されているかチェック
    if (!_interface) {
        log_printf(LOG_LEVEL_ERROR, "Network interface is not available");
        return;
    }
    
    // ソケットが開いているかチェック
    if (_socket.recvfrom(NULL, NULL, 0) == NSAPI_ERROR_NO_SOCKET) {
        log_printf(LOG_LEVEL_WARN, "UDP socket is not ready, reinitializing...");
        
        // UDPソケットを再初期化
        int init_attempts = 0;
        const int MAX_INIT_ATTEMPTS = 3;
        
        while (_running && init_attempts < MAX_INIT_ATTEMPTS) {
            if (init(_interface)) {
                log_printf(LOG_LEVEL_INFO, "UDP socket reinitialized successfully");
                break;
            } else {
                init_attempts++;
                log_printf(LOG_LEVEL_ERROR, "Failed to reinitialize UDP socket (attempt %d/%d)", init_attempts, MAX_INIT_ATTEMPTS);
                if (init_attempts < MAX_INIT_ATTEMPTS) {
                    log_printf(LOG_LEVEL_INFO, "Retrying in 2 seconds...");
                    ThisThread::sleep_for(2s);
                }
            }
        }
        
        if (init_attempts >= MAX_INIT_ATTEMPTS) {
            log_printf(LOG_LEVEL_ERROR, "Failed to reinitialize UDP socket after %d attempts", MAX_INIT_ATTEMPTS);
            return;
        }
    } else {
        log_printf(LOG_LEVEL_INFO, "UDP socket is already ready");
    }
    
    // Connection status counter
    int packet_count = 0;
    int last_packet_count = 0;
    time_t last_status_time = time(NULL);
    SocketAddress last_remote_addr;
    SocketAddress current_remote_addr;
    
    // Get debug level from config manager
    int debug_level = _config_manager ? _config_manager->getDebugLevel() : 0;
    log_printf(LOG_LEVEL_INFO, "UDP debug level: %d", debug_level);
    
    // ステータス表示の間隔（10秒）
    const time_t status_interval_time = 10;
    
    // エラーカウンター
    int error_count = 0;
    const int MAX_ERRORS = 5;
    
    // ソケット再初期化のカウンター
    int reinit_count = 0;
    const int MAX_REINIT = 3;
    
    // パケット処理時間の監視
    const uint32_t MAX_PROCESS_TIME = 100;  // 最大処理時間（ミリ秒）
    uint32_t total_process_time = 0;        // 合計処理時間
    uint32_t max_process_time = 0;          // 最大処理時間
    uint32_t process_count = 0;             // 処理したパケット数
    
    // 待機時間の設定
    const auto MAIN_LOOP_WAIT = 10ms;
    const auto ERROR_WAIT = 50ms;
    const auto REINIT_WAIT = 500ms;
    const auto MAX_REINIT_WAIT = 2s;
    
    while (_running) {
        // ミスト制御の状態チェック
        if (_mist_active) {
            uint32_t current_time = us_ticker_read() / 1000;  // 現在時刻（ミリ秒）
            uint32_t elapsed = current_time - _mist_start_time;
            
            if (elapsed >= _mist_duration) {
                // ミスト時間が経過したらOFFにする
                _rgb_led_driver.setColor(1, 0, 0, 0);
                _mist_active = false;
                log_printf(LOG_LEVEL_DEBUG, "Mist control completed after %d ms", elapsed);
            }
        }

        // Clear buffer
        memset(_recv_buffer, 0, MAX_BUFFER_SIZE);
        
        // Receive UDP packet
        nsapi_size_or_error_t result = _socket.recvfrom(&_remote_addr, _recv_buffer, MAX_BUFFER_SIZE - 1);
        
        if (result > 0) {
            // パケット処理開始時間を記録
            uint32_t start_time = us_ticker_read() / 1000;
            
            // Reset error counter on successful reception
            error_count = 0;
            
            // Null terminate the received data
            _recv_buffer[result] = '\0';
            
            // Process received packet
            packet_count++;
            last_remote_addr = _remote_addr;
            
            // Call packet received callback
            if (_packet_callback) {
                _packet_callback(_recv_buffer);
            }
            
            // Always log packet reception at debug level 1 or higher
            if (debug_level >= 1) {
                log_printf(LOG_LEVEL_INFO, "UDP packet received from %s:%d (%d bytes)", 
                          _remote_addr.get_ip_address(), _remote_addr.get_port(), result);
            }
            
            // Log packet contents at debug level 2 or higher
            if (debug_level >= 2) {
                log_printf(LOG_LEVEL_DEBUG, "Packet data: %s", _recv_buffer);
            }
            
            // Process command
            processCommand(_recv_buffer, result);
            
            // パケット処理時間を計算
            uint32_t process_time = (us_ticker_read() / 1000) - start_time;
            
            // 統計情報を更新
            total_process_time += process_time;
            process_count++;
            if (process_time > max_process_time) {
                max_process_time = process_time;
            }
            
            // 処理時間が長すぎる場合に警告
            if (process_time > MAX_PROCESS_TIME) {
                log_printf(LOG_LEVEL_WARN, "Command processing took %d ms", process_time);
            }
            
            // 定期的に統計情報を表示
            if (process_count % 100 == 0) {  // 100パケットごとに表示
                uint32_t avg_process_time = total_process_time / process_count;
                log_printf(LOG_LEVEL_INFO, "Packet processing stats - Avg: %d ms, Max: %d ms, Total packets: %d", 
                          avg_process_time, max_process_time, process_count);
            }
        } else if (result < 0 && result != NSAPI_ERROR_WOULD_BLOCK) {
            log_printf(LOG_LEVEL_ERROR, "UDP reception error: %d", result);
            error_count++;
            
            if (error_count >= MAX_ERRORS) {
                log_printf(LOG_LEVEL_ERROR, "Too many reception errors, reinitializing socket...");
                _socket.close();
                
                if (reinit_count >= MAX_REINIT) {
                    log_printf(LOG_LEVEL_ERROR, "Maximum reinitialization attempts reached");
                    ThisThread::sleep_for(MAX_REINIT_WAIT);
                    reinit_count = 0;
                }
                
                if (!init(_interface)) {
                    log_printf(LOG_LEVEL_ERROR, "Socket reinitialization failed");
                    ThisThread::sleep_for(REINIT_WAIT);
                    reinit_count++;
                    continue;
                }
                error_count = 0;
                reinit_count = 0;
            }
            
            ThisThread::sleep_for(ERROR_WAIT);
        }
        
        // Add short wait time to reduce CPU usage
        ThisThread::sleep_for(MAIN_LOOP_WAIT);
    }
    
    log_printf(LOG_LEVEL_INFO, "UDP thread stopped");
}

void UDPController::processCommand(const char* command, int length) {
    // コマンドを小文字に変換
    char cmd[MAX_BUFFER_SIZE];
    strncpy(cmd, command, length);
    cmd[length] = '\0';
    for (char* p = cmd; *p; p++) {
        *p = tolower(*p);
    }

    // コマンドの実行
    if (strcmp(cmd, "help") == 0) {
        // ヘルプメッセージを2分割して送信
        snprintf(_send_buffer, MAX_BUFFER_SIZE, 
            "Available commands (Part 1/2):\n"
            "help - Show this help\n"
            "debug level <0-3> - Set debug level\n"
            "debug status - Show current debug level\n"
            "config - Show all configuration\n"
            "config ssrlink <on/off> - Set SSR-LED link\n"
            "config ssrlink status - Show SSR-LED link status\n"
            "config rgb0 <led_id> <r> <g> <b> - Set LED 0%% color\n"
            "config rgb0 status <led_id> - Get LED 0%% color\n"
            "config rgb100 <led_id> <r> <g> <b> - Set LED 100%% color\n"
            "config rgb100 status <led_id> - Get LED 100%% color\n"
            "config trans <ms> - Set transition time\n"
            "config trans status - Get transition time\n"
            "config ssr_freq <freq> - Set SSR PWM frequency (-1-10 Hz, -1=設定変更無効)\n"
            "config ssr_freq status - Get SSR PWM frequency\n"
            "config ssr_freq status <id> - Get SSR PWM frequency for specific ID\n"
            "config load - Load configuration\n"
            "config save - Save configuration");
        sendResponse(_send_buffer);
        
        // 2番目のパートを送信
        snprintf(_send_buffer, MAX_BUFFER_SIZE,
            "Available commands (Part 2/2):\n"
            "reboot - Reboot device\n"
            "info - Show system information\n"
            "set <channel> <duty> - Set SSR duty cycle\n"
            "get <channel> - Get SSR duty cycle\n"
            "rgb <led_id> <r> <g> <b> - Set RGB LED color\n"
            "rgbget <led_id> - Get RGB LED color\n"
            "ws2812 <system> <led_id> <r> <g> <b> - Set WS2812 LED color\n"
            "ws2812get <system> <led_id> - Get WS2812 LED color\n"
            "ws2812sys <system> <r> <g> <b> - Set WS2812 system color\n"
            "ws2812off <system> - Turn off WS2812 system\n"
            "freq <channel> <freq> - Set SSR frequency\n"
            "zerox - Show zero-cross detection status");
        sendResponse(_send_buffer);
    }
    else if (strncmp(cmd, "debug level ", 12) == 0) {
        int level = atoi(cmd + 12);
        if (level >= 0 && level <= 3) {
            _config_manager->setDebugLevel(level);
            snprintf(_send_buffer, MAX_BUFFER_SIZE, "Debug level set to: %d", level);
            sendResponse(_send_buffer);
        } else {
            snprintf(_send_buffer, MAX_BUFFER_SIZE, "Error: Invalid debug level. Must be 0-3");
            sendResponse(_send_buffer);
        }
    }
    else if (strcmp(cmd, "debug status") == 0) {
        snprintf(_send_buffer, MAX_BUFFER_SIZE, "Current debug level: %d", _config_manager->getDebugLevel());
        sendResponse(_send_buffer);
    }
    else if (strcmp(cmd, "config") == 0) {
        // コンフィグ情報一覧を表示
        snprintf(_send_buffer, MAX_BUFFER_SIZE,
            "Configuration:\n"
            "SSR-LED Link: %s\n"
            "Transition Time: %d ms\n"
            "Debug Level: %d",
            _config_manager->isSSRLinkEnabled() ? "Enabled" : "Disabled",
            _config_manager->getSSRLinkTransitionTime(),
            _config_manager->getDebugLevel());
        sendResponse(_send_buffer);
    }
    else if (strncmp(cmd, "config ssrlink ", 15) == 0) {
        const char* value = cmd + 15;
        if (strcmp(value, "on") == 0 || strcmp(value, "1") == 0) {
            _config_manager->setSSRLink(true);
            snprintf(_send_buffer, MAX_BUFFER_SIZE, "SSR-LED link enabled");
            sendResponse(_send_buffer);
        } else if (strcmp(value, "off") == 0 || strcmp(value, "0") == 0) {
            _config_manager->setSSRLink(false);
            snprintf(_send_buffer, MAX_BUFFER_SIZE, "SSR-LED link disabled");
            sendResponse(_send_buffer);
        } else if (strcmp(value, "status") == 0) {
            snprintf(_send_buffer, MAX_BUFFER_SIZE, "SSR-LED link is %s", 
                _config_manager->isSSRLinkEnabled() ? "enabled" : "disabled");
            sendResponse(_send_buffer);
        } else {
            snprintf(_send_buffer, MAX_BUFFER_SIZE, "Error: Invalid command");
            sendResponse(_send_buffer);
        }
    }
    else if (strncmp(cmd, "config rgb0 ", 12) == 0) {
        const char* args = cmd + 12;
        if (strncmp(args, "status ", 7) == 0) {
            // 設定色を読み取るコマンド
            int led_id;
            if (sscanf(args + 7, "%d", &led_id) == 1) {
                if (led_id >= 1 && led_id <= 4) {
                    RGBColorData color = _config_manager->getSSRLinkColor0(led_id);
                    snprintf(_send_buffer, MAX_BUFFER_SIZE, "LED%d 0%% color: R:%d G:%d B:%d", 
                        led_id, color.r, color.g, color.b);
                    sendResponse(_send_buffer);
                } else {
                    snprintf(_send_buffer, MAX_BUFFER_SIZE, "Error: Invalid LED ID (1-4)");
                    sendResponse(_send_buffer);
                }
            } else {
                snprintf(_send_buffer, MAX_BUFFER_SIZE, "Error: Invalid command format");
                sendResponse(_send_buffer);
            }
        } else {
            // 設定色を設定するコマンド（既存）
            int led_id, r, g, b;
            if (sscanf(args, "%d,%d,%d,%d", &led_id, &r, &g, &b) == 4) {
                if (led_id >= 1 && led_id <= 4 &&
                    r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                    _config_manager->setSSRLinkColor0(led_id, r, g, b);
                    snprintf(_send_buffer, MAX_BUFFER_SIZE, "LED%d 0%% color set to R:%d G:%d B:%d", 
                        led_id, r, g, b);
                    sendResponse(_send_buffer);
                } else {
                    snprintf(_send_buffer, MAX_BUFFER_SIZE, "Error: Invalid parameters");
                    sendResponse(_send_buffer);
                }
            } else {
                snprintf(_send_buffer, MAX_BUFFER_SIZE, "Error: Invalid command format");
                sendResponse(_send_buffer);
            }
        }
    }
    else if (strncmp(cmd, "config rgb100 ", 14) == 0) {
        const char* args = cmd + 14;
        if (strncmp(args, "status ", 7) == 0) {
            // 設定色を読み取るコマンド
            int led_id;
            if (sscanf(args + 7, "%d", &led_id) == 1) {
                if (led_id >= 1 && led_id <= 4) {
                    RGBColorData color = _config_manager->getSSRLinkColor100(led_id);
                    snprintf(_send_buffer, MAX_BUFFER_SIZE, "LED%d 100%% color: R:%d G:%d B:%d", 
                        led_id, color.r, color.g, color.b);
                    sendResponse(_send_buffer);
                } else {
                    snprintf(_send_buffer, MAX_BUFFER_SIZE, "Error: Invalid LED ID (1-4)");
                    sendResponse(_send_buffer);
                }
            } else {
                snprintf(_send_buffer, MAX_BUFFER_SIZE, "Error: Invalid command format");
                sendResponse(_send_buffer);
            }
        } else {
            // 設定色を設定するコマンド（既存）
            int led_id, r, g, b;
            if (sscanf(args, "%d,%d,%d,%d", &led_id, &r, &g, &b) == 4) {
                if (led_id >= 1 && led_id <= 4 &&
                    r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                    _config_manager->setSSRLinkColor100(led_id, r, g, b);
                    snprintf(_send_buffer, MAX_BUFFER_SIZE, "LED%d 100%% color set to R:%d G:%d B:%d", 
                        led_id, r, g, b);
                    sendResponse(_send_buffer);
                } else {
                    snprintf(_send_buffer, MAX_BUFFER_SIZE, "Error: Invalid parameters");
                    sendResponse(_send_buffer);
                }
            } else {
                snprintf(_send_buffer, MAX_BUFFER_SIZE, "Error: Invalid command format");
                sendResponse(_send_buffer);
            }
        }
    }
    else if (strncmp(cmd, "config trans ", 13) == 0 || strncmp(cmd, "config t ", 10) == 0) {
        const char* args = strncmp(cmd, "config trans ", 13) == 0 ? cmd + 13 : cmd + 10;
        if (strcmp(args, "status") == 0) {
            // トランジション時間を読み取るコマンド
            int ms = _config_manager->getSSRLinkTransitionTime();
            snprintf(_send_buffer, MAX_BUFFER_SIZE, "Transition time is %d ms", ms);
            sendResponse(_send_buffer);
        } else {
            // トランジション時間を設定するコマンド（既存）
            int ms = atoi(args);
            if (ms >= 100 && ms <= 10000) {
                _config_manager->setSSRLinkTransitionTime(ms);
                snprintf(_send_buffer, MAX_BUFFER_SIZE, "Transition time set to %d ms", ms);
                sendResponse(_send_buffer);
            } else {
                snprintf(_send_buffer, MAX_BUFFER_SIZE, "Error: Invalid transition time. Must be 100-10000 ms");
                sendResponse(_send_buffer);
            }
        }
    }
    else if (strncmp(cmd, "config random rgb status", 24) == 0) {
        uint8_t v = _config_manager->getRandomRGBTimeout10s();
        snprintf(_send_buffer, MAX_BUFFER_SIZE, "config random rgb status: %u", v);
        sendResponse(_send_buffer);
    }
    else if (strncmp(cmd, "config random rgb ", 18) == 0) {
        const char* args = cmd + 18;
        // 値は0..255、単位は10秒。0は無効
        int value = atoi(args);
        if (value < 0 || value > 255) {
            snprintf(_send_buffer, MAX_BUFFER_SIZE, "Error: Invalid value (0-255)");
            sendResponse(_send_buffer);
        } else {
            _config_manager->setRandomRGBTimeout10s((uint8_t)value);
            snprintf(_send_buffer, MAX_BUFFER_SIZE, "config random rgb set to %d (x10s)", value);
            sendResponse(_send_buffer);
        }
    }
    else if (strncmp(cmd, "config ssr_freq ", 16) == 0) {
        const char* args = cmd + 16;
        if (strncmp(args, "status ", 7) == 0) {
            // 個別SSRの周波数を読み取るコマンド
            int ssr_id;
            if (sscanf(args + 7, "%d", &ssr_id) == 1) {
                if (ssr_id >= 1 && ssr_id <= 4) {
                    int freq = _ssr_driver.getPWMFrequency(ssr_id);
                    if (freq == -1) {
                        snprintf(_send_buffer, MAX_BUFFER_SIZE, "SSR%d PWM frequency is -1 (設定変更無効)", ssr_id);
                    } else {
                        snprintf(_send_buffer, MAX_BUFFER_SIZE, "SSR%d PWM frequency is %d Hz", ssr_id, freq);
                    }
                    sendResponse(_send_buffer);
                } else {
                    snprintf(_send_buffer, MAX_BUFFER_SIZE, "Error: Invalid SSR ID (1-4)");
                    sendResponse(_send_buffer);
                }
            } else {
                snprintf(_send_buffer, MAX_BUFFER_SIZE, "Error: Invalid command format");
                sendResponse(_send_buffer);
            }
        } else if (strcmp(args, "status") == 0) {
            // 全体の周波数を読み取るコマンド（既存）
            snprintf(_send_buffer, MAX_BUFFER_SIZE, "SSR PWM frequencies:");
            sendResponse(_send_buffer);
            for (int i = 1; i <= 4; i++) {
                int freq = _config_manager->getSSRPWMFrequency(i);
                if (freq == -1) {
                    snprintf(_send_buffer, MAX_BUFFER_SIZE, "SSR%d: -1 (設定変更無効)", i);
                } else {
                    snprintf(_send_buffer, MAX_BUFFER_SIZE, "SSR%d: %d Hz", i, freq);
                }
                sendResponse(_send_buffer);
            }
        } else {
            // 周波数を設定するコマンド（既存）
            int freq = atoi(args);
            if (freq >= -1 && freq <= 10) {
                _config_manager->setSSRPWMFrequency(freq);
                if (freq == -1) {
                    snprintf(_send_buffer, MAX_BUFFER_SIZE, "All SSR PWM frequencies set to -1 (設定変更無効)");
                } else {
                    snprintf(_send_buffer, MAX_BUFFER_SIZE, "All SSR PWM frequencies set to %d Hz", freq);
                }
                sendResponse(_send_buffer);
            } else {
                snprintf(_send_buffer, MAX_BUFFER_SIZE, "Error: Invalid frequency (-1-10 Hz)");
                sendResponse(_send_buffer);
            }
        }
    }
    else if (strcmp(cmd, "config load") == 0) {
        _config_manager->loadConfig();
        snprintf(_send_buffer, MAX_BUFFER_SIZE, "Configuration loaded");
        sendResponse(_send_buffer);
    }
    else if (strcmp(cmd, "config save") == 0) {
        // 現在のSSR周波数設定をConfigDataに反映（自動保存は無効）
        for (int i = 1; i <= 4; i++) {
            int8_t current_freq = _ssr_driver.getPWMFrequency(i);
            _config_manager->setSSRPWMFrequency(i, current_freq, false);
        }
        
        _config_manager->saveConfig();
        snprintf(_send_buffer, MAX_BUFFER_SIZE, "Configuration saved (including current SSR frequencies)");
        sendResponse(_send_buffer);
    }
    else if (strncmp(cmd, "set ", 4) == 0) {
        processSetCommand(cmd + 4);
    } else if (strncmp(cmd, "ssr ", 4) == 0) {
        // SSR command is an alias for SET command
        processSetCommand(cmd + 4);
    } else if (strncmp(cmd, "freq ", 5) == 0) {
        processFreqCommand(cmd + 5);
    } else if (strncmp(cmd, "get ", 4) == 0) {
        processGetCommand(cmd + 4);
    } else if (strncmp(cmd, "rgb ", 4) == 0) {
        processRGBCommand(cmd + 4);
    } else if (strncmp(cmd, "rgbget ", 7) == 0) {
        processRGBGetCommand(cmd + 7);
    } else if (strncmp(cmd, "ws2812 ", 7) == 0) {
        processWS2812Command(cmd + 7);
    } else if (strncmp(cmd, "ws2812get ", 10) == 0) {
        processWS2812GetCommand(cmd + 10);
    } else if (strncmp(cmd, "ws2812sys ", 10) == 0) {
        processWS2812SysCommand(cmd + 10);
    } else if (strncmp(cmd, "ws2812off ", 10) == 0) {
        processWS2812OffCommand(cmd + 10);
    } else if (strncmp(cmd, "sofia", 5) == 0) {
        processSofiaCommand();
    } else if (strncmp(cmd, "info", 4) == 0) {
        processInfoCommand();
    } else if (strncmp(cmd, "mist ", 5) == 0) {
        processMistCommand(cmd + 5);
    } else if (strncmp(cmd, "air ", 4) == 0) {
        processAirCommand(cmd + 4);
    } else if (strcmp(cmd, "zerox") == 0) {
        processZeroCrossCommand();
    } else {
        // Unknown command
        snprintf(_send_buffer, MAX_BUFFER_SIZE, "Error: Unknown command");
        sendResponse(_send_buffer);
    }
}

void UDPController::processSetCommand(const char* args) {
    // Parse arguments
    int id;
    int value;
    char value_str[32] = {0};
    
    // First try to parse as string (for ON/OFF support)
    if (sscanf(args, "%d,%s", &id, value_str) != 2) {
        log_printf(LOG_LEVEL_WARN, "SET command parse error: %s", args);
        generateErrorResponse(args);
        return;
    }
    
    // Process ON/OFF
    if (strcasecmp(value_str, "ON") == 0) {
        value = 100;
    } else if (strcasecmp(value_str, "OFF") == 0) {
        value = 0;
    } else {
        // Parse as number
        if (sscanf(value_str, "%d", &value) != 1) {
            log_printf(LOG_LEVEL_WARN, "SET command value parse error: %s", value_str);
            generateErrorResponse(args);
            return;
        }
    }
    
    // Check parameters
    if (id < 0 || id > 4 || value < 0 || value > 100) {
        log_printf(LOG_LEVEL_WARN, "SET command parameter error: id=%d, value=%d", id, value);
        generateErrorResponse(args);
        return;
    }
    
    log_printf(LOG_LEVEL_DEBUG, "SET command: id=%d, value=%d", id, value);
    
    bool success = true;
    
    // id=0 targets all SSRs
    if (id == 0) {
        // Set all SSRs
        for (int i = 1; i <= 4; i++) {
            success &= _ssr_driver.setDutyLevel(i, value);
        }
    } else {
        // Set specific SSR
        success = _ssr_driver.setDutyLevel(id, value);
    }
    
    // Generate response
    snprintf(_send_buffer, MAX_BUFFER_SIZE, "set %d,%d,%s", 
             id, value, success ? "OK" : "ERROR");
    
    log_printf(success ? LOG_LEVEL_DEBUG : LOG_LEVEL_ERROR, 
               "SET command result: %s", success ? "SUCCESS" : "FAILED");
    
    // Send response
    sendResponse(_send_buffer);
}

void UDPController::processFreqCommand(const char* args) {
    // Parse arguments
    int id;
    int freq;
    
    if (sscanf(args, "%d,%d", &id, &freq) != 2) {
        generateErrorResponse(args);
        return;
    }
    
    // Check parameters
    if (id < 0 || id > 4 || freq < -1 || freq > 10) {
        generateErrorResponse(args);
        return;
    }
    
    bool success = true;
    
    // id=0 targets all SSRs
    if (id == 0) {
        // Set frequency for all SSRs
        success = _ssr_driver.setPWMFrequency(freq);
    } else {
        // Set frequency for individual SSR channel
        success = _ssr_driver.setPWMFrequency(id, freq);
    }
    
    // Generate response
    snprintf(_send_buffer, MAX_BUFFER_SIZE, "freq %d,%d,%s", 
             id, freq, success ? "OK" : "ERROR");
    
    // Send response
    sendResponse(_send_buffer);
}

void UDPController::processGetCommand(const char* args) {
    // Parse arguments
    int id;
    
    if (sscanf(args, "%d", &id) != 1) {
        generateErrorResponse(args);
        return;
    }
    
    // Check parameters
    if (id < 1 || id > 4) {
        generateErrorResponse(args);
        return;
    }
    
    // Get current SSR state
    uint8_t duty_level = _ssr_driver.getDutyLevel(id);
    uint8_t freq = _ssr_driver.getPWMFrequency();
    
    // Generate response
    snprintf(_send_buffer, MAX_BUFFER_SIZE, "get %d,%d,%d,OK", 
             id, duty_level, freq);
    
    // Send response
    sendResponse(_send_buffer);
}

void UDPController::processRGBCommand(const char* args) {
    // Parse arguments
    int id;
    int r, g, b;
    
    if (sscanf(args, "%d,%d,%d,%d", &id, &r, &g, &b) != 4) {
        log_printf(LOG_LEVEL_WARN, "RGB command parse error: %s", args);
        generateErrorResponse(args);
        return;
    }
    
    // Check parameters
    if (id < 0 || id > 4 || r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
        log_printf(LOG_LEVEL_WARN, "RGB command parameter error: id=%d, r=%d, g=%d, b=%d", id, r, g, b);
        generateErrorResponse(args);
        return;
    }
    
    log_printf(LOG_LEVEL_DEBUG, "RGB command: id=%d, r=%d, g=%d, b=%d", id, r, g, b);
    
    bool success = true;
    
    // id=0 targets all RGB LEDs
    if (id == 0) {
        // Set all RGB LEDs
        for (int i = 1; i <= 4; i++) {
            success &= _rgb_led_driver.setColor(i, r, g, b);
        }
    } else {
        // Set specific RGB LED
        success = _rgb_led_driver.setColor(id, r, g, b);
    }
    
    // Generate response
    snprintf(_send_buffer, MAX_BUFFER_SIZE, "rgb %d,%d,%d,%d,%s", 
             id, r, g, b, success ? "OK" : "ERROR");
    
    log_printf(success ? LOG_LEVEL_DEBUG : LOG_LEVEL_ERROR, 
               "RGB command result: %s", success ? "SUCCESS" : "FAILED");
    
    // Send response
    sendResponse(_send_buffer);
}

void UDPController::processRGBGetCommand(const char* args) {
    // Parse arguments
    int id;
    
    if (sscanf(args, "%d", &id) != 1) {
        generateErrorResponse(args);
        return;
    }
    
    // Check parameters
    if (id < 1 || id > 3) {
        generateErrorResponse(args);
        return;
    }
    
    uint8_t r, g, b;
    bool success = _rgb_led_driver.getColor(id, &r, &g, &b);
    
    if (success) {
        // Generate response
        snprintf(_send_buffer, MAX_BUFFER_SIZE, "rgbget %d,%d,%d,%d,OK", 
                id, r, g, b);
    } else {
        // Generate error response
        snprintf(_send_buffer, MAX_BUFFER_SIZE, "rgbget %d,ERROR", id);
    }
    
    // Send response
    sendResponse(_send_buffer);
}

void UDPController::processSofiaCommand() {
    // Cute Sofia
    strcpy(_send_buffer, "sofia,KAWAII,OK");
    
    // Send response
    sendResponse(_send_buffer);
}

void UDPController::processInfoCommand() {
    // バージョン情報を取得
    VersionInfo version = getVersionInfo();
    
    // Generate device information
    snprintf(_send_buffer, MAX_BUFFER_SIZE, "info,%s,%s,OK", 
             version.device, version.version);
    
    // Send response
    sendResponse(_send_buffer);
}

void UDPController::processMistCommand(const char* args) {
    // Parse arguments
    int duration;
    
    if (sscanf(args, "%d", &duration) != 1) {
        log_printf(LOG_LEVEL_WARN, "MIST command parse error: %s", args);
        generateErrorResponse(args);
        return;
    }
    
    // Check parameters
    if (duration < 0 || duration > 10000) {  // Maximum 10 seconds
        log_printf(LOG_LEVEL_WARN, "MIST command parameter error: duration=%d", duration);
        generateErrorResponse(args);
        return;
    }
    
    log_printf(LOG_LEVEL_DEBUG, "MIST command: duration=%d ms", duration);
    
    // Set all colors to 100% for RGB LED 1
    bool success = _rgb_led_driver.setColor(1, 255, 255, 255);
    
    // Set asynchronous processing parameters
    _mist_active = true;
    _mist_start_time = us_ticker_read() / 1000;  // Convert to milliseconds
    _mist_duration = duration;
    
    // Generate response
    snprintf(_send_buffer, MAX_BUFFER_SIZE, "mist %d,%s", 
             duration, success ? "OK" : "ERROR");
    
    // Send response
    sendResponse(_send_buffer);
}

void UDPController::processAirCommand(const char* args) {
    // Parse arguments
    int level;
    
    if (sscanf(args, "%d", &level) != 1) {
        log_printf(LOG_LEVEL_WARN, "AIR command parse error: %s", args);
        generateErrorResponse(args);
        return;
    }
    
    // Check parameters
    if (level < 0 || level > 2) {
        log_printf(LOG_LEVEL_WARN, "AIR command parameter error: level=%d", level);
        generateErrorResponse(args);
        return;
    }
    
    log_printf(LOG_LEVEL_DEBUG, "AIR command: level=%d", level);
    
    bool success = true;
    
    // Control LEDs according to level
    switch (level) {
        case 0:  // Turn off both
            success &= _rgb_led_driver.setColor(2, 0, 0, 0);
            success &= _rgb_led_driver.setColor(3, 0, 0, 0);
            break;
            
        case 1:  // Turn on LED2 only
            success &= _rgb_led_driver.setColor(2, 255, 255, 255);
            success &= _rgb_led_driver.setColor(3, 0, 0, 0);
            break;
            
        case 2:  // Turn on both
            success &= _rgb_led_driver.setColor(2, 255, 255, 255);
            success &= _rgb_led_driver.setColor(3, 255, 255, 255);
            break;
    }
    
    // Generate response
    snprintf(_send_buffer, MAX_BUFFER_SIZE, "air %d,%s", 
             level, success ? "OK" : "ERROR");
    
    // Send response
    sendResponse(_send_buffer);
}

void UDPController::processZeroCrossCommand() {
    // Get zero-cross statistics
    uint32_t count, interval;
    float frequency;
    _ssr_driver.getZeroCrossStats(count, interval, frequency);
    
    bool detected = _ssr_driver.isZeroCrossDetected();
    
    // Generate response
    snprintf(_send_buffer, MAX_BUFFER_SIZE, "zerox,%s,%lu,%lu,%.1f,OK", 
             detected ? "DETECTED" : "NOT_DETECTED", interval, count, frequency);
    
    // Send response
    sendResponse(_send_buffer);
}

void UDPController::processWS2812Command(const char* args) {
    // Parse arguments: system,led_id,r,g,b
    int system, led_id, r, g, b;
    
    if (sscanf(args, "%d,%d,%d,%d,%d", &system, &led_id, &r, &g, &b) != 5) {
        log_printf(LOG_LEVEL_WARN, "WS2812 command parse error: %s", args);
        generateErrorResponse(args);
        return;
    }
    
    // Check parameters
    if (system < 1 || system > 3 || 
        led_id < 1 || led_id > 256 ||
        r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
        log_printf(LOG_LEVEL_WARN, "WS2812 command parameter error: system=%d, led=%d, r=%d, g=%d, b=%d", 
                   system, led_id, r, g, b);
        generateErrorResponse(args);
        return;
    }
    
    log_printf(LOG_LEVEL_DEBUG, "WS2812 command: system=%d, led=%d, r=%d, g=%d, b=%d", 
               system, led_id, r, g, b);
    
    // Set WS2812 LED color
    bool success = _ws2812_driver.setColor(system, led_id, r, g, b);
    
    // Update the LED
    if (success) {
        success = _ws2812_driver.update(system);
    }
    
    // Generate response
    snprintf(_send_buffer, MAX_BUFFER_SIZE, "ws2812 %d,%d,%d,%d,%d,%s", 
             system, led_id, r, g, b, success ? "OK" : "ERROR");
    
    log_printf(success ? LOG_LEVEL_DEBUG : LOG_LEVEL_ERROR, 
               "WS2812 command result: %s", success ? "SUCCESS" : "FAILED");
    
    // Send response
    sendResponse(_send_buffer);
}

void UDPController::processWS2812GetCommand(const char* args) {
    // Parse arguments: system,led_id
    int system, led_id;
    
    if (sscanf(args, "%d,%d", &system, &led_id) != 2) {
        log_printf(LOG_LEVEL_WARN, "WS2812GET command parse error: %s", args);
        generateErrorResponse(args);
        return;
    }
    
    // Check parameters
    if (system < 1 || system > 3 || led_id < 1 || led_id > 256) {
        log_printf(LOG_LEVEL_WARN, "WS2812GET command parameter error: system=%d, led=%d", system, led_id);
        generateErrorResponse(args);
        return;
    }
    
    uint8_t r, g, b;
    bool success = _ws2812_driver.getColor(system, led_id, &r, &g, &b);
    
    if (success) {
        // Generate response
        snprintf(_send_buffer, MAX_BUFFER_SIZE, "ws2812get %d,%d,%d,%d,%d,OK", 
                system, led_id, r, g, b);
    } else {
        // Generate error response
        snprintf(_send_buffer, MAX_BUFFER_SIZE, "ws2812get %d,%d,ERROR", system, led_id);
    }
    
    // Send response
    sendResponse(_send_buffer);
}

void UDPController::processWS2812SysCommand(const char* args) {
    // Parse arguments: system,r,g,b
    int system, r, g, b;
    
    if (sscanf(args, "%d,%d,%d,%d", &system, &r, &g, &b) != 4) {
        log_printf(LOG_LEVEL_WARN, "WS2812SYS command parse error: %s", args);
        generateErrorResponse(args);
        return;
    }
    
    // Check parameters
    if (system < 1 || system > 3 ||
        r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
        log_printf(LOG_LEVEL_WARN, "WS2812SYS command parameter error: system=%d, r=%d, g=%d, b=%d", 
                   system, r, g, b);
        generateErrorResponse(args);
        return;
    }
    
    log_printf(LOG_LEVEL_DEBUG, "WS2812SYS command: system=%d, r=%d, g=%d, b=%d", 
               system, r, g, b);
    
    // Set all LEDs in the system to the same color
    bool success = _ws2812_driver.setSystemColor(system, r, g, b);
    
    // Update the system
    if (success) {
        success = _ws2812_driver.update(system);
    }
    
    // Generate response
    snprintf(_send_buffer, MAX_BUFFER_SIZE, "ws2812sys %d,%d,%d,%d,%s", 
             system, r, g, b, success ? "OK" : "ERROR");
    
    log_printf(success ? LOG_LEVEL_DEBUG : LOG_LEVEL_ERROR, 
               "WS2812SYS command result: %s", success ? "SUCCESS" : "FAILED");
    
    // Send response
    sendResponse(_send_buffer);
}

void UDPController::processWS2812OffCommand(const char* args) {
    // Parse arguments: system
    int system;
    
    if (sscanf(args, "%d", &system) != 1) {
        log_printf(LOG_LEVEL_WARN, "WS2812OFF command parse error: %s", args);
        generateErrorResponse(args);
        return;
    }
    
    // Check parameters
    if (system < 1 || system > 3) {
        log_printf(LOG_LEVEL_WARN, "WS2812OFF command parameter error: system=%d", system);
        generateErrorResponse(args);
        return;
    }
    
    log_printf(LOG_LEVEL_DEBUG, "WS2812OFF command: system=%d", system);
    
    // Turn off all LEDs in the system
    bool success = _ws2812_driver.turnOff(system);
    
    // Update the system
    if (success) {
        success = _ws2812_driver.update(system);
    }
    
    // Generate response
    snprintf(_send_buffer, MAX_BUFFER_SIZE, "ws2812off %d,%s", 
             system, success ? "OK" : "ERROR");
    
    log_printf(success ? LOG_LEVEL_DEBUG : LOG_LEVEL_ERROR, 
               "WS2812OFF command result: %s", success ? "SUCCESS" : "FAILED");
    
    // Send response
    sendResponse(_send_buffer);
}

void UDPController::generateErrorResponse(const char* command) {
    // Generate error response
    snprintf(_send_buffer, MAX_BUFFER_SIZE, "%s,ERROR", command);
    
    // Send response
    sendResponse(_send_buffer);
}

void UDPController::sendResponse(const char* response) {
    // Send UDP response
    log_printf(LOG_LEVEL_DEBUG, "UDP response send: %s", response);
    
    nsapi_size_or_error_t result = _socket.sendto(_remote_addr, response, strlen(response));
    
    if (result < 0) {
        log_printf(LOG_LEVEL_ERROR, "Response send error: %d", result);
    }
} 