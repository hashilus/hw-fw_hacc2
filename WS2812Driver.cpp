#include "WS2812Driver.h"
#include "PinNames.h"
#include "main.h"  // log_printfを使用するため

WS2812Driver::WS2812Driver()
    : _spi0(P10_14, NC, P10_12, NC),
      _spi1(P11_14, NC, P11_12, NC),
      _spi3(P5_2,   NC, P5_0,   NC)
{
    // SPI設定（8bit, mode0, 2.4MHz）
    _spi0.format(8, 0);
    _spi1.format(8, 0);
    _spi3.format(8, 0);
    _spi0.frequency(2400000);
    _spi1.frequency(2400000);
    _spi3.frequency(2400000);
    
    // Initialize color/transfer buffers
    memset(_colors, 0, sizeof(_colors));
    memset(_buffer0, 0, sizeof(_buffer0));
    memset(_buffer1, 0, sizeof(_buffer1));
    memset(_buffer3_buf, 0, sizeof(_buffer3_buf));
    
    // Turn off all LEDs initially
    allOff();
}

WS2812Driver::~WS2812Driver() {
    allOff();
}

// UART駆動は廃止（SPIへ移行）

bool WS2812Driver::setColor(uint8_t system, uint8_t led_id, uint8_t r, uint8_t g, uint8_t b) {
    // Check parameters
    if (system < 1 || system > WS2812_SYSTEMS || 
        led_id < 1 || led_id > WS2812_LED_COUNT) {
        return false;
    }
    
    // Convert to array index
    uint8_t sys_idx = system - 1;
    uint8_t led_idx = led_id - 1;
    
    // Store color data
    _colors[sys_idx][led_idx][0] = r;
    _colors[sys_idx][led_idx][1] = g;
    _colors[sys_idx][led_idx][2] = b;
    
    return true;
}

bool WS2812Driver::setSystemColor(uint8_t system, uint8_t r, uint8_t g, uint8_t b) {
    // Check system parameter
    if (system < 1 || system > WS2812_SYSTEMS) {
        return false;
    }
    
    // Set all LEDs in the system to the same color
    for (uint8_t led_id = 1; led_id <= WS2812_LED_COUNT; led_id++) {
        if (!setColor(system, led_id, r, g, b)) {
            return false;
        }
    }
    
    return true;
}

bool WS2812Driver::update(uint8_t system) {
    // Check system parameter
    if (system < 1 || system > WS2812_SYSTEMS) {
        return false;
    }
    
    uint8_t sys_idx = system - 1;
    uint8_t* buffer;
    SPI* spi;
    
    // Select buffer and SPI for the system
    switch (system) {
        case 1:
            buffer = _buffer0; // SPI0
            spi = &_spi0;
            break;
        case 2:
            buffer = _buffer1; // SPI1
            spi = &_spi1;
            break;
        case 3:
            buffer = _buffer3_buf; // SPI3
            spi = &_spi3;
            break;
        default:
            return false;
    }
    
    // Convert all LED colors to SPI-encoded WS2812 stream (9 bytes per LED)
    for (int i = 0; i < WS2812_LED_COUNT; i++) {
        uint8_t r = _colors[sys_idx][i][0];
        uint8_t g = _colors[sys_idx][i][1];
        uint8_t b = _colors[sys_idx][i][2];
        encodeGRBToSPI(r, g, b, &buffer[i * 9]);
    }
    
    // Send WS2812 data using SPI
    sendWS2812Data(*spi, buffer, WS2812_LED_COUNT * 9);
    
    return true;
}

bool WS2812Driver::updateAll() {
    bool success = true;
    
    // Update all systems
    for (uint8_t system = 1; system <= WS2812_SYSTEMS; system++) {
        if (!update(system)) {
            success = false;
        }
    }
    
    return success;
}

bool WS2812Driver::turnOff(uint8_t system) {
    return setSystemColor(system, 0, 0, 0);
}

bool WS2812Driver::allOff() {
    bool success = true;
    
    // Turn off all systems
    for (uint8_t system = 1; system <= WS2812_SYSTEMS; system++) {
        if (!turnOff(system)) {
            success = false;
        }
    }
    
    // Update all systems
    if (success) {
        success = updateAll();
    }
    
    return success;
}

bool WS2812Driver::getColor(uint8_t system, uint8_t led_id, uint8_t* r, uint8_t* g, uint8_t* b) {
    // Check parameters
    if (system < 1 || system > WS2812_SYSTEMS || 
        led_id < 1 || led_id > WS2812_LED_COUNT ||
        r == nullptr || g == nullptr || b == nullptr) {
        return false;
    }
    
    // Convert to array index
    uint8_t sys_idx = system - 1;
    uint8_t led_idx = led_id - 1;
    
    // Get color data
    *r = _colors[sys_idx][led_idx][0];
    *g = _colors[sys_idx][led_idx][1];
    *b = _colors[sys_idx][led_idx][2];
    
    return true;
}

// rgbToWS2812 廃止（SPI方式へ移行）

void WS2812Driver::sendWS2812Data(SPI& spi, const uint8_t* buffer, int length) {
    // 送信（RX不要）
    spi.write((const char*)buffer, length, nullptr, 0);
    // リセット >80us
    wait_us(100);
}

void WS2812Driver::encodeByteTo24Bits(uint8_t value, uint8_t* out3) {
    uint32_t acc = 0;
    for (int i = 7; i >= 0; i--) {
        bool bit = (value >> i) & 0x01;
        uint32_t symbol = bit ? 0b110 : 0b100; // 3-bit symbol
        acc = (acc << 3) | symbol;
    }
    out3[0] = (uint8_t)((acc >> 16) & 0xFF);
    out3[1] = (uint8_t)((acc >> 8) & 0xFF);
    out3[2] = (uint8_t)(acc & 0xFF);
}

void WS2812Driver::encodeGRBToSPI(uint8_t r, uint8_t g, uint8_t b, uint8_t* out9) {
    encodeByteTo24Bits(g, &out9[0]);
    encodeByteTo24Bits(r, &out9[3]);
    encodeByteTo24Bits(b, &out9[6]);
}