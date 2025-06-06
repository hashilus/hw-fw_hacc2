#pragma once

#include "mbed.h"
#include "EthernetInterface.h"
#include "ConfigManager.h"
#include "main.h"

class NetworkManager {
public:
    NetworkManager(ConfigManager* config_manager);
    ~NetworkManager();

    bool init();
    bool connect();
    void disconnect();
    bool isConnected() const { return _connected; }
    EthernetInterface* get_interface() { return &_interface; }
    const EthernetInterface* get_interface() const { return &_interface; }
    const char* get_ip_address() const { return _ip_address; }
    const char* get_netmask() const { return _netmask; }
    const char* get_gateway() const { return _gateway; }
    const char* get_mac_address() const { return _mac_address; }

    bool set_dhcp(bool enabled);
    bool set_network(const uint8_t* ip, const uint8_t* netmask, const uint8_t* gateway);

private:
    EthernetInterface _interface;
    ConfigManager* _config_manager;
    bool _connected;
    bool _running;
    char _ip_address[16];
    char _netmask[16];
    char _gateway[16];
    char _mac_address[18];
    Thread _thread;

    void _update_network_info();
    void _thread_func();
};