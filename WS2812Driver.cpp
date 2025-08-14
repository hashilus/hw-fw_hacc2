#include "WS2812Driver.h"
#include "PinNames.h"
#include "main.h"  // log_printfを使用するため

// RZ-A1H ポート反転レジスタ定義
#undef PNOT2_BASE
#undef PNOT5_BASE
#undef PNOT2
#undef PNOT5
#define PNOT2_BASE (0xFCFE3708UL)
#define PNOT5_BASE (0xFCFE3714UL)
#define PNOT2 (*(volatile uint16_t *)(PNOT2_BASE))
#define PNOT5 (*(volatile uint16_t *)(PNOT5_BASE))

WS2812Driver::WS2812Driver()
    : _uart1(P5_0, NC, 2400000),  // System 1: P5_0
      _uart2(P5_3, NC, 2400000),  // System 2: P5_3
      _uart3(P2_14, NC, 2400000)  // System 3: P2_14
{
    log_printf(LOG_LEVEL_DEBUG, "WS2812Driver: Starting initialization");
    ThisThread::sleep_for(10ms);  // 出力完了を待つ
    
    // Initialize port inversion for UART TX
    log_printf(LOG_LEVEL_DEBUG, "WS2812Driver: Initializing port inversion");
    ThisThread::sleep_for(5ms);  // 出力完了を待つ
    initPortInversion();
    log_printf(LOG_LEVEL_DEBUG, "WS2812Driver: Port inversion initialized");
    ThisThread::sleep_for(5ms);  // 出力完了を待つ
    
    // Initialize color buffers
    log_printf(LOG_LEVEL_DEBUG, "WS2812Driver: Initializing color buffers");
    ThisThread::sleep_for(5ms);  // 出力完了を待つ
    memset(_colors, 0, sizeof(_colors));
    memset(_buffer1, 0, sizeof(_buffer1));
    memset(_buffer2, 0, sizeof(_buffer2));
    memset(_buffer3, 0, sizeof(_buffer3));
    log_printf(LOG_LEVEL_DEBUG, "WS2812Driver: Color buffers initialized");
    ThisThread::sleep_for(5ms);  // 出力完了を待つ
    
    // Turn off all LEDs initially
    log_printf(LOG_LEVEL_DEBUG, "WS2812Driver: Turning off all LEDs");
    ThisThread::sleep_for(5ms);  // 出力完了を待つ
    allOff();
    log_printf(LOG_LEVEL_DEBUG, "WS2812Driver: All LEDs turned off");
    ThisThread::sleep_for(5ms);  // 出力完了を待つ
    
    log_printf(LOG_LEVEL_DEBUG, "WS2812Driver: Initialization completed");
    ThisThread::sleep_for(10ms);  // 出力完了を待つ
}

WS2812Driver::~WS2812Driver() {
    allOff();
}

void WS2812Driver::initPortInversion() {
    log_printf(LOG_LEVEL_DEBUG, "[DEBUG] WS2812: Setting port inversion for UART TX pins");
    ThisThread::sleep_for(5ms);

    // P5_0, P5_3, P2_14のUART TX出力を反転
    log_printf(LOG_LEVEL_DEBUG, "[DEBUG] WS2812: PNOT5 |= (1 << 0) | (1 << 3)");
    ThisThread::sleep_for(5ms);
    PNOT5 |= (1 << 0) | (1 << 3);
    log_printf(LOG_LEVEL_DEBUG, "[DEBUG] WS2812: PNOT5 set");
    ThisThread::sleep_for(5ms);

    log_printf(LOG_LEVEL_DEBUG, "[DEBUG] WS2812: PNOT2 |= (1 << 14)");
    ThisThread::sleep_for(5ms);
    PNOT2 |= (1 << 14);
    log_printf(LOG_LEVEL_DEBUG, "[DEBUG] WS2812: PNOT2 set");
    ThisThread::sleep_for(5ms);

    log_printf(LOG_LEVEL_DEBUG, "[DEBUG] WS2812: Port inversion completed");
    ThisThread::sleep_for(5ms);
}

void WS2812Driver::initDMA() {
    // DMA initialization temporarily disabled
    // Will be implemented when DMAC API is available
}

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
    BufferedSerial* uart;
    
    // Select buffer and UART for the system
    switch (system) {
        case 1:
            buffer = _buffer1;
            uart = &_uart1;
            break;
        case 2:
            buffer = _buffer2;
            uart = &_uart2;
            break;
        case 3:
            buffer = _buffer3;
            uart = &_uart3;
            break;
        default:
            return false;
    }
    
    // Convert all LED colors to WS2812 signal
    for (int i = 0; i < WS2812_LED_COUNT; i++) {
        uint8_t r = _colors[sys_idx][i][0];
        uint8_t g = _colors[sys_idx][i][1];
        uint8_t b = _colors[sys_idx][i][2];
        
        // Convert RGB to WS2812 signal (24 bits per LED)
        rgbToWS2812(r, g, b, &buffer[i * 24]);
    }
    
    // Send WS2812 data using UART
    sendWS2812Data(*uart, buffer, WS2812_LED_COUNT * 24);
    
    return true;
}

bool WS2812Driver::updateAll() {
    log_printf(LOG_LEVEL_DEBUG, "WS2812: Starting updateAll operation");
    ThisThread::sleep_for(5ms);  // 出力完了を待つ
    
    bool success = true;
    
    // Update all systems
    for (uint8_t system = 1; system <= WS2812_SYSTEMS; system++) {
        log_printf(LOG_LEVEL_DEBUG, "WS2812: Updating system %d", system);
        ThisThread::sleep_for(5ms);  // 出力完了を待つ
        if (!update(system)) {
            success = false;
        }
        log_printf(LOG_LEVEL_DEBUG, "WS2812: System %d updated", system);
        ThisThread::sleep_for(5ms);  // 出力完了を待つ
    }
    
    log_printf(LOG_LEVEL_DEBUG, "WS2812: updateAll operation completed");
    ThisThread::sleep_for(5ms);  // 出力完了を待つ
    
    return success;
}

bool WS2812Driver::turnOff(uint8_t system) {
    return setSystemColor(system, 0, 0, 0);
}

bool WS2812Driver::allOff() {
    log_printf(LOG_LEVEL_DEBUG, "WS2812: Starting allOff operation");
    ThisThread::sleep_for(5ms);  // 出力完了を待つ
    
    bool success = true;
    
    // Turn off all systems
    for (uint8_t system = 1; system <= WS2812_SYSTEMS; system++) {
        log_printf(LOG_LEVEL_DEBUG, "WS2812: Turning off system %d", system);
        ThisThread::sleep_for(5ms);  // 出力完了を待つ
        if (!turnOff(system)) {
            success = false;
        }
        log_printf(LOG_LEVEL_DEBUG, "WS2812: System %d turned off", system);
        ThisThread::sleep_for(5ms);  // 出力完了を待つ
    }
    
    // Update all systems
    if (success) {
        log_printf(LOG_LEVEL_DEBUG, "WS2812: Updating all systems");
        ThisThread::sleep_for(5ms);  // 出力完了を待つ
        success = updateAll();
        log_printf(LOG_LEVEL_DEBUG, "WS2812: All systems updated");
        ThisThread::sleep_for(5ms);  // 出力完了を待つ
    }
    
    log_printf(LOG_LEVEL_DEBUG, "WS2812: allOff operation completed");
    ThisThread::sleep_for(5ms);  // 出力完了を待つ
    
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

void WS2812Driver::rgbToWS2812(uint8_t r, uint8_t g, uint8_t b, uint8_t* buffer) {
    // WS2812 uses GRB order
    uint32_t color = (g << 16) | (r << 8) | b;
    
    // Convert 24-bit color to WS2812 signal
    for (int i = 23; i >= 0; i--) {
        uint8_t bit = (color >> i) & 1;
        buffer[23 - i] = bit ? WS2812_1_BIT : WS2812_0_BIT;
    }
}

void WS2812Driver::sendWS2812Data(BufferedSerial& uart, uint8_t* buffer, int length) {
    // Send WS2812 data via UART
    uart.write(buffer, length);
    
    // WS2812 requires a reset signal (low for >50μs)
    // UART idle state (high) provides this automatically
    // Wait a bit to ensure reset signal
    wait_us(100);
} 