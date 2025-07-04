#include "SerialController.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "main.h"
#include "version.h"
#include "mbed.h"

// Version information
#define DEVICE_NAME "ACpowerController"
#define DEVICE_VERSION "Ver 1.0.0"

SerialController::SerialController(ConfigManager* config_manager, SSRDriver* ssr_driver, RGBLEDDriver* rgb_led_driver, BufferedSerial& pc)
    : _pc(pc)
    , _config_manager(config_manager)
    , _ssr_driver(ssr_driver)
    , _rgb_led_driver(rgb_led_driver)
    , _recv_index(0)
    , _command_complete(false)
    , _command_callback(nullptr)
    , _history_index(-1)
    , _cursor_position(0) {
    memset(_recv_buffer, 0, sizeof(_recv_buffer));
}

SerialController::~SerialController() {
}

bool SerialController::init() {
    log_printf(LOG_LEVEL_INFO, "Initializing serial communication...");
    memset(_recv_buffer, 0, sizeof(_recv_buffer));
    _recv_index = 0;
    log_printf(LOG_LEVEL_INFO, "Serial communication ready");
    return true;
}

bool SerialController::run() {
    log_printf(LOG_LEVEL_INFO, "Starting command processing loop...");
    displayHelp();
    
    while (true) {
        processSerialInput();
        wait_us(1000);  // 1ms wait
    }
    
    return true;
}

void SerialController::processSerialInput() {
    if (!_pc.readable()) {
        return;
    }

    char c;
    if (_pc.read(&c, 1) == 1) {
        // ANSIエスケープシーケンスの検出（ESC [）
        if (c == 0x1B) {
            char next;
            if (_pc.read(&next, 1) == 1 && next == '[') {
                char arrow;
                if (_pc.read(&arrow, 1) == 1) {
                    switch (arrow) {
                        case 'A':  // 上矢印
                            if (_history_index > 0) {
                                _history_index--;
                                showHistoryCommand(_history_index);
                            }
                            break;
                        case 'B':  // 下矢印
                            if (_history_index < _command_history.size() - 1) {
                                _history_index++;
                                showHistoryCommand(_history_index);
                            } else {
                                _history_index = _command_history.size();
                                clearLine();
                                memset(_recv_buffer, 0, sizeof(_recv_buffer));
                                _recv_index = 0;
                                _cursor_position = 0;
                                _pc.write("\n> ", 3);  // 改行とプロンプトを表示
                                _pc.sync();  // バッファをフラッシュして即座に表示
                            }
                            break;
                        case 'C':  // 右矢印
                            if (_cursor_position < _recv_index) {
                                moveCursor(_cursor_position + 1);
                            }
                            break;
                        case 'D':  // 左矢印
                            if (_cursor_position > 0) {
                                moveCursor(_cursor_position - 1);
                            }
                            break;
                    }
                }
            }
            return;
        }
        
        // バックスペース（0x08）またはデリート（0x7F）キーの処理
        if (c == 0x08 || c == 0x7F) {
            if (_cursor_position > 0) {
                // カーソル位置の文字を削除
                memmove(&_recv_buffer[_cursor_position - 1], 
                        &_recv_buffer[_cursor_position], 
                        _recv_index - _cursor_position);
                _recv_index--;
                _recv_buffer[_recv_index] = '\0';
                _cursor_position--;
                
                // 行を再描画
                clearLine();
                _pc.write("> ", 2);  // プロンプトを表示
                _pc.write(_recv_buffer, _recv_index);  // バッファの内容を表示
                moveCursor(_cursor_position);
            }
            return;
        }
        
        // 通常の文字の処理
        if (c == '\n' || c == '\r') {
            if (_recv_index > 0) {
                _recv_buffer[_recv_index] = '\0';
                addToHistory(_recv_buffer);
                processCommand(_recv_buffer);
                _recv_index = 0;
                _cursor_position = 0;
                memset(_recv_buffer, 0, sizeof(_recv_buffer));
                _pc.write("\n", 1);  // 改行を表示
                _pc.sync();  // バッファをフラッシュして即座に表示
            }
        } else if (_recv_index < sizeof(_recv_buffer) - 1) {
            // カーソル位置に文字を挿入
            memmove(&_recv_buffer[_cursor_position + 1], 
                    &_recv_buffer[_cursor_position], 
                    _recv_index - _cursor_position);
            _recv_buffer[_cursor_position] = c;
            _recv_index++;
            _cursor_position++;
            
            // 行を再描画
            if (_cursor_position == 1) {
                _pc.write("> ", 2);  // 最初の文字の場合はプロンプトを表示
                _pc.write(&c, 1);  // 文字を表示
            } else {
                _pc.write(&c, 1);  // 文字のみ表示
            }
            if (_cursor_position < _recv_index) {
                _pc.write(&_recv_buffer[_cursor_position], _recv_index - _cursor_position);
                moveCursor(_cursor_position);
            }
        }
    }
}

void SerialController::processCommand(const char* command) {
    // コマンドを小文字に変換
    char cmd[MAX_BUFFER_SIZE];
    strncpy(cmd, command, MAX_BUFFER_SIZE - 1);
    cmd[MAX_BUFFER_SIZE - 1] = '\0';
    for (char* p = cmd; *p; p++) {
        *p = tolower(*p);
    }

    // コマンドの実行
    if (strcmp(cmd, "help") == 0) {
        displayHelp();
    }
    else if (strncmp(cmd, "debug level ", 12) == 0) {
        int level = atoi(cmd + 12);
        if (level >= 0 && level <= 3) {
            _config_manager->setDebugLevel(level);
            log_printf(LOG_LEVEL_INFO, "Debug level set to: %d", level);
        } else {
            log_printf(LOG_LEVEL_ERROR, "Invalid debug level. Must be 0-3");
        }
    }
    else if (strcmp(cmd, "debug status") == 0) {
        log_printf(LOG_LEVEL_INFO, "Current debug level: %d", _config_manager->getDebugLevel());
    }
    else if (strcmp(cmd, "reboot") == 0) {
        handleRebootCommand();
    } else if (strncmp(cmd, "set ", 4) == 0) {
        handleSetCommand(cmd + 4);
    } else if (strncmp(cmd, "freq ", 5) == 0) {
        handleFreqCommand(cmd + 5);
    } else if (strncmp(cmd, "get ", 4) == 0) {
        handleGetCommand(cmd + 4);
    } else if (strncmp(cmd, "rgb ", 4) == 0) {
        handleRGBCommand(cmd + 4);
    } else if (strncmp(cmd, "rgbget ", 7) == 0) {
        handleRGBGetCommand(cmd + 7);
    } else if (strcmp(cmd, "info") == 0) {
        handleInfoCommand();
    } else if (strcmp(cmd, "config") == 0) {
        handleConfigCommand("config");  // コンフィグ情報一覧を表示
    } else if (strncmp(cmd, "config ", 7) == 0) {
        handleConfigCommand(cmd + 7);
    }
    else {
        log_printf(LOG_LEVEL_ERROR, "Unknown command: %s", cmd);
        log_printf(LOG_LEVEL_INFO, "Type 'help' to see available commands");
    }
}

void SerialController::displayHelp() {
    log_printf(LOG_LEVEL_INFO, "=== Available Commands ===");
    log_printf(LOG_LEVEL_INFO, "SSR Control:");
    log_printf(LOG_LEVEL_INFO, "  set <num>,<value>    Set SSR output (0-100%%)");
    log_printf(LOG_LEVEL_INFO, "  freq <num>,<hz>      Set PWM frequency (1-10Hz)");
    log_printf(LOG_LEVEL_INFO, "  get <num>            Get current settings");
    
    log_printf(LOG_LEVEL_INFO, "RGB LED Control:");
    log_printf(LOG_LEVEL_INFO, "  rgb <num>,<r>,<g>,<b>  Set RGB LED color (0-255, num=1-4)");
    log_printf(LOG_LEVEL_INFO, "  rgbget <num>         Get RGB LED color (num=1-4)");
    
    log_printf(LOG_LEVEL_INFO, "Configuration:");
    log_printf(LOG_LEVEL_INFO, "  config               Display all configuration");
    log_printf(LOG_LEVEL_INFO, "  config save          Save current settings");
    log_printf(LOG_LEVEL_INFO, "  config load          Reload settings");
    log_printf(LOG_LEVEL_INFO, "  config ssrlink on/off  Enable/disable SSR-LED link");
    log_printf(LOG_LEVEL_INFO, "  config netbios <name>  Set NETBIOS name");
    log_printf(LOG_LEVEL_INFO, "  config ip <ip>       Set IP address");
    log_printf(LOG_LEVEL_INFO, "  config mask <mask>   Set netmask");
    log_printf(LOG_LEVEL_INFO, "  config gateway <gw>  Set gateway");
    log_printf(LOG_LEVEL_INFO, "  config dhcp on/off   Enable/disable DHCP");
    log_printf(LOG_LEVEL_INFO, "  config rgb0 <n>,<r>,<g>,<b>  Set SSR 0%% color");
    log_printf(LOG_LEVEL_INFO, "  config rgb100 <n>,<r>,<g>,<b>  Set SSR 100%% color");
    log_printf(LOG_LEVEL_INFO, "  config trans <ms>  Set transition time (100-10000ms)");

    log_printf(LOG_LEVEL_INFO, "Debug:");
    log_printf(LOG_LEVEL_INFO, "  debug level <0-3>    Set debug level");
    log_printf(LOG_LEVEL_INFO, "  debug status         Show current debug level");
    
    log_printf(LOG_LEVEL_INFO, "System:");
    log_printf(LOG_LEVEL_INFO, "  info                 Display device information");
    log_printf(LOG_LEVEL_INFO, "  reboot               Restart the system");
    log_printf(LOG_LEVEL_INFO, "  help                 Show this help message");
    log_printf(LOG_LEVEL_INFO, "============================");
}

void SerialController::handleRebootCommand() {
    log_printf(LOG_LEVEL_INFO, "System is rebooting...");
    log_printf(LOG_LEVEL_INFO, "Please wait...");
    
    // 少し待機してログが出力されるのを待つ
    ThisThread::sleep_for(100ms);
    
    // システムを再起動
    NVIC_SystemReset();
}

void SerialController::handleSetCommand(const char* command) {
    int num, value;
    if (sscanf(command, "%d,%d", &num, &value) == 2) {
        if (num >= 1 && num <= 4 && value >= 0 && value <= 100) {
            _ssr_driver->setDutyLevel(num, value);
            log_printf(LOG_LEVEL_INFO, "SSR%d set to %d%%", num, value);
        } else {
            log_printf(LOG_LEVEL_ERROR, "Invalid parameters");
        }
    } else {
        log_printf(LOG_LEVEL_ERROR, "Invalid format. Use: set <num>,<value>");
    }
}

void SerialController::handleFreqCommand(const char* command) {
    int num, freq;
    if (sscanf(command, "%d,%d", &num, &freq) == 2) {
        if (num >= 1 && num <= 4 && freq >= 1 && freq <= 10) {
            _ssr_driver->setPWMFrequency(freq);
            log_printf(LOG_LEVEL_INFO, "SSR%d frequency set to %d Hz", num, freq);
        } else {
            log_printf(LOG_LEVEL_ERROR, "Invalid parameters");
        }
    } else {
        log_printf(LOG_LEVEL_ERROR, "Invalid format. Use: freq <num>,<hz>");
    }
}

void SerialController::handleGetCommand(const char* command) {
    int num;
    if (sscanf(command, "%d", &num) == 1) {
        if (num >= 1 && num <= 4) {
            log_printf(LOG_LEVEL_INFO, "SSR%d: %d%% (%d Hz)", 
                      num, 
                      _ssr_driver->getDutyLevel(num),
                      _ssr_driver->getPWMFrequency());
        } else {
            log_printf(LOG_LEVEL_ERROR, "Invalid SSR number");
        }
    } else {
        log_printf(LOG_LEVEL_ERROR, "Invalid format. Use: get <num>");
    }
}

void SerialController::handleRGBCommand(const char* command) {
    int num, r, g, b;
    if (sscanf(command, "%d,%d,%d,%d", &num, &r, &g, &b) == 4) {
        if (num >= 1 && num <= 4 && 
            r >= 0 && r <= 255 && 
            g >= 0 && g <= 255 && 
            b >= 0 && b <= 255) {
            _rgb_led_driver->setColor(num, r, g, b);
            log_printf(LOG_LEVEL_INFO, "LED%d color set to R:%d G:%d B:%d", num, r, g, b);
        } else {
            log_printf(LOG_LEVEL_ERROR, "Invalid parameters");
        }
    } else {
        log_printf(LOG_LEVEL_ERROR, "Invalid format. Use: rgb <num>,<red>,<green>,<blue>");
    }
}

void SerialController::handleRGBGetCommand(const char* command) {
    int num;
    if (sscanf(command, "%d", &num) == 1) {
        if (num >= 1 && num <= 4) {
            uint8_t r, g, b;
            if (_rgb_led_driver->getColor(num, &r, &g, &b)) {
                log_printf(LOG_LEVEL_INFO, "LED%d color: R:%d G:%d B:%d", num, r, g, b);
            } else {
                log_printf(LOG_LEVEL_ERROR, "Failed to get LED color");
            }
        } else {
            log_printf(LOG_LEVEL_ERROR, "Invalid LED number");
        }
    } else {
        log_printf(LOG_LEVEL_ERROR, "Invalid format. Use: rgbget <num>");
    }
}

void SerialController::handleInfoCommand() {
    // バージョン情報を取得
    VersionInfo version = getVersionInfo();
    
    log_printf(LOG_LEVEL_INFO, "Device Information:");
    log_printf(LOG_LEVEL_INFO, "- Device: %s", version.device);
    log_printf(LOG_LEVEL_INFO, "- Version: %s", version.version);
    log_printf(LOG_LEVEL_INFO, "- CPU: %s", MBED_STRINGIFY(TARGET_CPU));
    log_printf(LOG_LEVEL_INFO, "- Mbed OS Version: %d.%d.%d", 
               MBED_MAJOR_VERSION, MBED_MINOR_VERSION, MBED_PATCH_VERSION);
    log_printf(LOG_LEVEL_INFO, "- Build Date: %s %s", __DATE__, __TIME__);
    
    if (_config_manager) {
        log_printf(LOG_LEVEL_INFO, "Network Settings:");
        log_printf(LOG_LEVEL_INFO, "- DHCP: %s", _config_manager->isDHCPEnabled() ? "Enabled" : "Disabled");
        log_printf(LOG_LEVEL_INFO, "- IP: %s", _config_manager->getIPAddress());
        log_printf(LOG_LEVEL_INFO, "- Netmask: %s", _config_manager->getNetmask());
        log_printf(LOG_LEVEL_INFO, "- Gateway: %s", _config_manager->getGateway());
        log_printf(LOG_LEVEL_INFO, "- NETBIOS: %s", _config_manager->getNetBIOSName());
        log_printf(LOG_LEVEL_INFO, "- UDP Port: %d", _config_manager->getUDPPort());
        
        log_printf(LOG_LEVEL_INFO, "System Settings:");
        log_printf(LOG_LEVEL_INFO, "- Debug Level: %d", _config_manager->getDebugLevel());
        log_printf(LOG_LEVEL_INFO, "- SSR-LED Link: %s", _config_manager->isSSRLinkEnabled() ? "Enabled" : "Disabled");
        log_printf(LOG_LEVEL_INFO, "- Transition Time: %d ms", _config_manager->getSSRLinkTransitionTime());
    }
}

void SerialController::handleConfigCommand(const char* command) {
    if (strcmp(command, "config") == 0) {
        log_printf(LOG_LEVEL_INFO, "=== Current Configuration ===");
        
        // システム情報
        log_printf(LOG_LEVEL_INFO, "System Information:");
        log_printf(LOG_LEVEL_INFO, "- Device: %s", MBED_STRINGIFY(TARGET_NAME));
        log_printf(LOG_LEVEL_INFO, "- CPU: %s", MBED_STRINGIFY(TARGET_CPU));
        log_printf(LOG_LEVEL_INFO, "- Mbed OS: %d.%d.%d", 
                   MBED_MAJOR_VERSION, MBED_MINOR_VERSION, MBED_PATCH_VERSION);
        log_printf(LOG_LEVEL_INFO, "- Build: %s %s", __DATE__, __TIME__);
        log_printf(LOG_LEVEL_INFO, "------------------------------------------");
        
        // ネットワーク設定
        log_printf(LOG_LEVEL_INFO, "Network Settings:");
        log_printf(LOG_LEVEL_INFO, "- DHCP: %s", _config_manager->isDHCPEnabled() ? "Enabled" : "Disabled");
        log_printf(LOG_LEVEL_INFO, "- IP: %s", _config_manager->getIPAddress());
        log_printf(LOG_LEVEL_INFO, "- Netmask: %s", _config_manager->getNetmask());
        log_printf(LOG_LEVEL_INFO, "- Gateway: %s", _config_manager->getGateway());
        log_printf(LOG_LEVEL_INFO, "- NETBIOS: %s", _config_manager->getNetBIOSName());
        log_printf(LOG_LEVEL_INFO, "- UDP Port: %d", _config_manager->getUDPPort());
        log_printf(LOG_LEVEL_INFO, "------------------------------------------");
        
        // SSR-LED連動設定
        log_printf(LOG_LEVEL_INFO, "SSR-LED Link Settings:");
        log_printf(LOG_LEVEL_INFO, "- Status: %s", _config_manager->isSSRLinkEnabled() ? "Enabled" : "Disabled");
        log_printf(LOG_LEVEL_INFO, "- Transition Time: %d ms", _config_manager->getSSRLinkTransitionTime());
        
        // RGB LED色設定
        log_printf(LOG_LEVEL_INFO, "RGB LED Colors:");
        for (int i = 1; i <= 4; i++) {
            RGBColorData color0 = _config_manager->getSSRLinkColor0(i);
            RGBColorData color100 = _config_manager->getSSRLinkColor100(i);
            log_printf(LOG_LEVEL_INFO, "SSR%d:", i);
            log_printf(LOG_LEVEL_INFO, "  0%%: R:%d G:%d B:%d", color0.r, color0.g, color0.b);
            log_printf(LOG_LEVEL_INFO, "  100%%: R:%d G:%d B:%d", color100.r, color100.g, color100.b);
        }
        log_printf(LOG_LEVEL_INFO, "------------------------------------------");
        
        // 通信設定
        log_printf(LOG_LEVEL_INFO, "Communication Settings:");
        log_printf(LOG_LEVEL_INFO, "- UDP Port: %d", _config_manager->getUDPPort());
        log_printf(LOG_LEVEL_INFO, "- Serial: 115200 bps, 8N1");
        log_printf(LOG_LEVEL_INFO, "------------------------------------------");
        
        // デバッグ設定
        log_printf(LOG_LEVEL_INFO, "Debug Settings:");
        log_printf(LOG_LEVEL_INFO, "- Level: %d", _config_manager->getDebugLevel());
        log_printf(LOG_LEVEL_INFO, "==========================================");
    }
    else if (strcmp(command, "save") == 0) {
        if (_config_manager->saveConfig()) {
            log_printf(LOG_LEVEL_INFO, "Configuration saved successfully");
        } else {
            log_printf(LOG_LEVEL_ERROR, "Failed to save configuration");
        }
    }
    else if (strncmp(command, "ssrlink ", 8) == 0) {
        const char* value = command + 8;
        if (strcmp(value, "on") == 0 || strcmp(value, "1") == 0) {
            _config_manager->setSSRLink(true);
            log_printf(LOG_LEVEL_INFO, "SSR-LED link enabled");
        } else if (strcmp(value, "off") == 0 || strcmp(value, "0") == 0) {
            _config_manager->setSSRLink(false);
            log_printf(LOG_LEVEL_INFO, "SSR-LED link disabled");
        } else {
            log_printf(LOG_LEVEL_ERROR, "Invalid value. Use 'on'/'1' or 'off'/'0'");
        }
    }
    else if (strncmp(command, "netbios ", 8) == 0) {
        const char* value = command + 8;
        if (_config_manager->setNetBIOSName(value)) {
            log_printf(LOG_LEVEL_INFO, "NETBIOS name set to: %s", value);
        } else {
            log_printf(LOG_LEVEL_ERROR, "Invalid NETBIOS name. Must be 1-15 characters");
        }
    }
    else if (strcmp(command, "netbios") == 0) {
        log_printf(LOG_LEVEL_INFO, "Current NETBIOS name: %s", _config_manager->getNetBIOSName());
    }
    else if (strncmp(command, "ip ", 3) == 0) {
        const char* value = command + 3;
        if (_config_manager->setIPAddress(value)) {
            log_printf(LOG_LEVEL_INFO, "IP address set to: %s", value);
        } else {
            log_printf(LOG_LEVEL_ERROR, "Invalid IP address format");
        }
    }
    else if (strncmp(command, "mask ", 5) == 0) {
        const char* value = command + 5;
        if (_config_manager->setNetmask(value)) {
            log_printf(LOG_LEVEL_INFO, "Netmask set to: %s", value);
        } else {
            log_printf(LOG_LEVEL_ERROR, "Invalid netmask format");
        }
    }
    else if (strncmp(command, "gateway ", 8) == 0) {
        const char* value = command + 8;
        if (_config_manager->setGateway(value)) {
            log_printf(LOG_LEVEL_INFO, "Gateway set to: %s", value);
        } else {
            log_printf(LOG_LEVEL_ERROR, "Invalid gateway format");
        }
    }
    else if (strncmp(command, "dhcp ", 5) == 0) {
        const char* value = command + 5;
        if (strcmp(value, "on") == 0 || strcmp(value, "1") == 0) {
            _config_manager->setDHCPEnabled(true);
            log_printf(LOG_LEVEL_INFO, "DHCP enabled");
        } else if (strcmp(value, "off") == 0 || strcmp(value, "0") == 0) {
            _config_manager->setDHCPEnabled(false);
            log_printf(LOG_LEVEL_INFO, "DHCP disabled");
        } else {
            log_printf(LOG_LEVEL_ERROR, "Invalid value. Use 'on'/'1' or 'off'/'0'");
        }
    }
    else if (strncmp(command, "rgb0 ", 5) == 0) {
        int num, r, g, b;
        if (sscanf(command + 5, "%d,%d,%d,%d", &num, &r, &g, &b) == 4) {
            if (num >= 1 && num <= 4 && r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                _config_manager->setSSRLinkColor0(num, r, g, b);
                log_printf(LOG_LEVEL_INFO, "SSR%d 0%% color set to R:%d G:%d B:%d", num, r, g, b);
            } else {
                log_printf(LOG_LEVEL_ERROR, "Invalid parameters");
            }
        } else {
            log_printf(LOG_LEVEL_ERROR, "Invalid format. Use: rgb0 <num>,<red>,<green>,<blue>");
        }
    }
    else if (strncmp(command, "rgb100 ", 7) == 0) {
        int num, r, g, b;
        if (sscanf(command + 7, "%d,%d,%d,%d", &num, &r, &g, &b) == 4) {
            if (num >= 1 && num <= 4 && r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                _config_manager->setSSRLinkColor100(num, r, g, b);
                log_printf(LOG_LEVEL_INFO, "SSR%d 100%% color set to R:%d G:%d B:%d", num, r, g, b);
            } else {
                log_printf(LOG_LEVEL_ERROR, "Invalid parameters");
            }
        } else {
            log_printf(LOG_LEVEL_ERROR, "Invalid format. Use: rgb100 <num>,<red>,<green>,<blue>");
        }
    }
    else if (strcmp(command, "load") == 0) {
        if (_config_manager->loadConfig()) {
            log_printf(LOG_LEVEL_INFO, "Configuration loaded successfully");
        } else {
            log_printf(LOG_LEVEL_ERROR, "Failed to load configuration");
        }
    }
    else if (strncmp(command, "trans ", 6) == 0) {
        int ms;
        if (sscanf(command + 6, "%d", &ms) == 1) {
            _config_manager->setSSRLinkTransitionTime(ms);
            log_printf(LOG_LEVEL_INFO, "Transition time set to %d ms", ms);
        } else {
            log_printf(LOG_LEVEL_ERROR, "Invalid format. Use: trans <ms>");
        }
    }
    else {
        log_printf(LOG_LEVEL_ERROR, "Unknown config command");
    }
}

void SerialController::addToHistory(const std::string& command) {
    if (command.empty()) return;
    
    // 同じコマンドが連続して入力された場合は追加しない
    if (!_command_history.empty() && _command_history.back() == command) {
        return;
    }
    
    _command_history.push_back(command);
    if (_command_history.size() > MAX_HISTORY_SIZE) {
        _command_history.erase(_command_history.begin());
    }
    _history_index = _command_history.size();
}

void SerialController::showHistoryCommand(int index) {
    clearLine();
    if (index >= 0 && index < _command_history.size()) {
        strncpy(_recv_buffer, _command_history[index].c_str(), sizeof(_recv_buffer) - 1);
        _recv_buffer[sizeof(_recv_buffer) - 1] = '\0';
        _recv_index = strlen(_recv_buffer);
        _cursor_position = _recv_index;
        _pc.write("> ", 2);  // プロンプトを表示
        _pc.write(_recv_buffer, _recv_index);  // コマンドを表示
    }
}

void SerialController::moveCursor(int position) {
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "\r\033[%dC", position + 2);  // プロンプトの長さ（"> "）を考慮
    _pc.write(cmd, strlen(cmd));
}

void SerialController::clearLine() {
    _pc.write("\r\033[K", 4);  // カーソルを行頭に移動し、行をクリア
}

void SerialController::redrawLine() {
    clearLine();
    _pc.write("> ", 2);  // プロンプトを表示
    _pc.write(_recv_buffer, _recv_index);  // バッファの内容を表示
    _cursor_position = _recv_index;
}
