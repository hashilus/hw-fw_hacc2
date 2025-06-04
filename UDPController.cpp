#include "UDPController.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "main.h"
#include "netsocket/NetworkInterface.h"
#include "netsocket/NetworkStack.h"
#include "netsocket/SocketAddress.h"
#include "netsocket/UDPSocket.h"
#include "netsocket/InternetSocket.h"

UDPController::UDPController(SSRDriver& ssr_driver, RGBLEDDriver& rgb_led_driver, ConfigManager* config_manager)
    : _ssr_driver(ssr_driver), _rgb_led_driver(rgb_led_driver), 
      _packet_callback(nullptr), _command_callback(nullptr),
      _config_manager(config_manager), _mist_active(false),
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
    _running = false;
    if (_thread.get_state() == Thread::Running) {
        _thread.join();
    }
}

bool UDPController::init(NetworkInterface* interface) {
    if (!interface) {
        log_printf(LOG_LEVEL_ERROR, "Network interface is not initialized");
        return false;
    }

    _interface = interface;

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

    // Set socket buffer sizes
    const int RECV_BUFFER_SIZE = 8192;  // 8KB
    const int SEND_BUFFER_SIZE = 8192;  // 8KB
    
    if (_socket.setsockopt(NSAPI_SOCKET, NSAPI_RCVBUF, &RECV_BUFFER_SIZE, sizeof(RECV_BUFFER_SIZE)) != 0) {
        log_printf(LOG_LEVEL_WARN, "Failed to set receive buffer size");
    } else {
        log_printf(LOG_LEVEL_INFO, "Receive buffer size set to %d bytes", RECV_BUFFER_SIZE);
    }
    
    if (_socket.setsockopt(NSAPI_SOCKET, NSAPI_SNDBUF, &SEND_BUFFER_SIZE, sizeof(SEND_BUFFER_SIZE)) != 0) {
        log_printf(LOG_LEVEL_WARN, "Failed to set send buffer size");
    } else {
        log_printf(LOG_LEVEL_INFO, "Send buffer size set to %d bytes", SEND_BUFFER_SIZE);
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
    if (_running) {
        log_printf(LOG_LEVEL_WARN, "UDP thread is already running");
        return false;
    }

    _running = true;
    _thread.start(callback(this, &UDPController::_thread_func));
    log_printf(LOG_LEVEL_INFO, "UDP thread started");
    return true;
}

void UDPController::_thread_func() {
    log_printf(LOG_LEVEL_INFO, "UDP thread started");
    
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
    // Call packet received callback
    if (_packet_callback) {
        _packet_callback(command);
    }
    
    // Call command executed callback
    if (_command_callback) {
        _command_callback(command);
    }
    
    log_printf(LOG_LEVEL_DEBUG, "Processing command: %s", command);
    
    // Separate command name and arguments
    char cmd[32] = {0};  // Buffer for command name
    const char* args = nullptr;  // Pointer to argument part
    
    // Extract command name (alphabets only)
    size_t i = 0;
    while (i < static_cast<size_t>(length) && i < sizeof(cmd) - 1) {
        if (isalpha(command[i])) {
            cmd[i] = command[i];
            i++;
        } else {
            break;
        }
    }
    cmd[i] = '\0';
    
    // Set argument part
    if (i < static_cast<size_t>(length)) {
        args = &command[i];
    }
    
    // Execute processing according to command
    if (strcmp(cmd, "set") == 0) {
        processSetCommand(args);
    } else if (strcmp(cmd, "ssr") == 0) {
        // SSR command is an alias for SET command
        processSetCommand(args);
    } else if (strcmp(cmd, "freq") == 0) {
        processFreqCommand(args);
    } else if (strcmp(cmd, "get") == 0) {
        processGetCommand(args);
    } else if (strcmp(cmd, "rgb") == 0) {
        processRGBCommand(args);
    } else if (strcmp(cmd, "rgbget") == 0) {
        processRGBGetCommand(args);
    } else if (strcmp(cmd, "sofia") == 0) {
        processSofiaCommand();
    } else if (strcmp(cmd, "info") == 0) {
        processInfoCommand();
    } else if (strcmp(cmd, "mist") == 0) {
        processMistCommand(args);
    } else if (strcmp(cmd, "air") == 0) {
        processAirCommand(args);
    } else {
        // Unknown command
        log_printf(LOG_LEVEL_ERROR, "Unknown command: %s", command);
        generateErrorResponse(command);
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
            // Enable PWM mode
            if (value > 0) {
                _ssr_driver.enablePWM(i, true);
            }
            success &= _ssr_driver.setDutyLevel(i, value);
        }
    } else {
        // Set specific SSR
        if (value > 0) {
            _ssr_driver.enablePWM(id, true);
        }
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
    if (id < 0 || id > 4 || freq < 1 || freq > 10) {
        generateErrorResponse(args);
        return;
    }
    
    bool success = true;
    
    // id=0 targets all SSRs
    if (id == 0) {
        // Set frequency for all SSRs
        success = _ssr_driver.setPWMFrequency(freq);
    } else {
        // Individual SSR frequency setting is not supported yet,
        // but keep the condition branch for future expansion
        success = _ssr_driver.setPWMFrequency(freq);
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
    if (id < 0 || id > 3 || r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
        log_printf(LOG_LEVEL_WARN, "RGB command parameter error: id=%d, r=%d, g=%d, b=%d", id, r, g, b);
        generateErrorResponse(args);
        return;
    }
    
    log_printf(LOG_LEVEL_DEBUG, "RGB command: id=%d, r=%d, g=%d, b=%d", id, r, g, b);
    
    bool success = true;
    
    // id=0 targets all RGB LEDs
    if (id == 0) {
        // Set all RGB LEDs
        for (int i = 1; i <= 3; i++) {
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
    // Generate device information
    snprintf(_send_buffer, MAX_BUFFER_SIZE, "info,%s,2.0.0,OK", 
             DEVICE_NAME);
    
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