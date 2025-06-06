#include "NetworkManager.h"
#include "mbed-trace/mbed_trace.h"
#include "main.h"  // log_printfの定義を含む
#include "mbed.h"  // ThisThreadの定義を含む
#include "rtos.h"  // Threadの定義を含む
#include <string.h>
#include <stdio.h>

#define TRACE_GROUP "NETM"

NetworkManager::NetworkManager(ConfigManager* config_manager)
    : _config_manager(config_manager), _connected(false), _running(false) {
    memset(_ip_address, 0, sizeof(_ip_address));
    memset(_netmask, 0, sizeof(_netmask));
    memset(_gateway, 0, sizeof(_gateway));
    memset(_mac_address, 0, sizeof(_mac_address));
}

NetworkManager::~NetworkManager() {
    disconnect();
}

bool NetworkManager::init() {
    log_printf(LOG_LEVEL_INFO, "Initializing network interface...");
    
    // Get network settings from config manager
    if (_config_manager) {
        const char* ip = _config_manager->getIPAddress();
        const char* netmask = _config_manager->getNetmask();
        const char* gateway = _config_manager->getGateway();
        bool dhcp = _config_manager->isDHCPEnabled();
        
        if (dhcp) {
            log_printf(LOG_LEVEL_INFO, "Using DHCP for network configuration");
            if (_interface.set_dhcp(true) != 0) {
                log_printf(LOG_LEVEL_ERROR, "Failed to enable DHCP");
                return false;
            }
        } else {
            log_printf(LOG_LEVEL_INFO, "Using static IP: %s", ip);
            if (_interface.set_network(ip, netmask, gateway) != 0) {
                log_printf(LOG_LEVEL_ERROR, "Failed to set static IP");
                return false;
            }
        }
    }
    
    // ネットワークインターフェースの初期化を確認
    if (_interface.get_connection_status() != NSAPI_STATUS_DISCONNECTED) {
        log_printf(LOG_LEVEL_WARN, "Network interface is not in disconnected state");
        _interface.disconnect();
        ThisThread::sleep_for(1s);  // 1秒待機
    }
    
    log_printf(LOG_LEVEL_INFO, "Network interface initialized");
    return true;
}

bool NetworkManager::set_dhcp(bool enabled) {
    log_printf(LOG_LEVEL_INFO, "Setting DHCP: %s", enabled ? "enabled" : "disabled");
    
    if (_interface.set_dhcp(enabled) != 0) {
        log_printf(LOG_LEVEL_ERROR, "Failed to set DHCP");
        return false;
    }
    
    return true;
}

bool NetworkManager::set_network(const uint8_t* ip, const uint8_t* netmask, const uint8_t* gateway) {
    if (!ip || !netmask || !gateway) {
        log_printf(LOG_LEVEL_ERROR, "Invalid network parameters");
        return false;
    }
    
    char ip_str[16];
    char mask_str[16];
    char gw_str[16];
    
    // IPアドレスを文字列に変換（ホストバイトオーダー）
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
             ip[0], ip[1], ip[2], ip[3]);
    
    // サブネットマスクを文字列に変換（ホストバイトオーダー）
    snprintf(mask_str, sizeof(mask_str), "%d.%d.%d.%d",
             netmask[0], netmask[1], netmask[2], netmask[3]);
    
    // ゲートウェイを文字列に変換（ホストバイトオーダー）
    snprintf(gw_str, sizeof(gw_str), "%d.%d.%d.%d",
             gateway[0], gateway[1], gateway[2], gateway[3]);
    
    log_printf(LOG_LEVEL_INFO, "Setting static IP: %s", ip_str);
    log_printf(LOG_LEVEL_INFO, "Setting netmask: %s", mask_str);
    log_printf(LOG_LEVEL_INFO, "Setting gateway: %s", gw_str);
    
    if (_interface.set_network(ip_str, mask_str, gw_str) != 0) {
        log_printf(LOG_LEVEL_ERROR, "Failed to set network parameters");
        return false;
    }

    return true;
}

bool NetworkManager::connect() {
    if (_connected) {
        log_printf(LOG_LEVEL_WARN, "Already connected");
        return true;
    }
    
    log_printf(LOG_LEVEL_INFO, "Connecting to network...");
    
    // 接続試行回数の制限
    const int MAX_RETRIES = 3;
    int retry_count = 0;
    
    while (retry_count < MAX_RETRIES) {
        // Connect to network
        if (_interface.connect() == 0) {
            _connected = true;
            _update_network_info();
            
            log_printf(LOG_LEVEL_INFO, "Connected to network");
            log_printf(LOG_LEVEL_INFO, "IP address: %s", _ip_address);
            log_printf(LOG_LEVEL_INFO, "Netmask: %s", _netmask);
            log_printf(LOG_LEVEL_INFO, "Gateway: %s", _gateway);
            log_printf(LOG_LEVEL_INFO, "MAC address: %s", _mac_address);
            
            return true;
        }
        
        log_printf(LOG_LEVEL_WARN, "Connection attempt %d failed", retry_count + 1);
        retry_count++;
        ThisThread::sleep_for(2s);  // 2秒待機
    }
    
    log_printf(LOG_LEVEL_ERROR, "Failed to connect after %d attempts", MAX_RETRIES);
            return false;
        }

void NetworkManager::disconnect() {
    if (!_connected) {
        return;
    }
    
    log_printf(LOG_LEVEL_INFO, "Disconnecting from network...");
    
    _interface.disconnect();
    _connected = false;
    
    log_printf(LOG_LEVEL_INFO, "Disconnected from network");
}

void NetworkManager::_update_network_info() {
    SocketAddress ip_addr;
    SocketAddress netmask_addr;
    SocketAddress gateway_addr;
    
    // Get IP address
    if (_interface.get_ip_address(&ip_addr) == 0) {
        strncpy(_ip_address, ip_addr.get_ip_address(), sizeof(_ip_address) - 1);
        _ip_address[sizeof(_ip_address) - 1] = '\0';
    }
    
    // Get netmask
    if (_interface.get_netmask(&netmask_addr) == 0) {
        strncpy(_netmask, netmask_addr.get_ip_address(), sizeof(_netmask) - 1);
        _netmask[sizeof(_netmask) - 1] = '\0';
    }
    
    // Get gateway
    if (_interface.get_gateway(&gateway_addr) == 0) {
        strncpy(_gateway, gateway_addr.get_ip_address(), sizeof(_gateway) - 1);
        _gateway[sizeof(_gateway) - 1] = '\0';
}

    // Get MAC address
    const char* mac = _interface.get_mac_address();
    if (mac) {
        strncpy(_mac_address, mac, sizeof(_mac_address) - 1);
        _mac_address[sizeof(_mac_address) - 1] = '\0';
    }
}

void NetworkManager::_thread_func() {
    while (_running) {
        // Check network connection status
        if (_interface.get_connection_status() != NSAPI_STATUS_GLOBAL_UP) {
            log_printf(LOG_LEVEL_WARN, "Network connection lost, attempting to reconnect...");
            _interface.disconnect();
            ThisThread::sleep_for(2s);  // Wait 2 seconds
            _interface.connect();
            
            // Check reconnection status
            if (_interface.get_connection_status() == NSAPI_STATUS_GLOBAL_UP) {
                log_printf(LOG_LEVEL_INFO, "Network reconnected successfully");
                
                // Get and display new IP address
                SocketAddress addr;
                if (_interface.get_ip_address(&addr) == 0) {
                    log_printf(LOG_LEVEL_INFO, "New IP address: %s", addr.get_ip_address());
                }
            } else {
                log_printf(LOG_LEVEL_ERROR, "Network reconnection failed");
            }
        }
        
        ThisThread::sleep_for(1s);  // Check every second
    }
} 