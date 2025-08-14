#ifndef WS2812_DRIVER_H
#define WS2812_DRIVER_H

#include "mbed.h"

// WS2812制御用定数
#define WS2812_LED_COUNT 256  // 各系統のLED数
#define WS2812_SYSTEMS 3     // 系統数

// WS2812信号タイミング（UART 2.4MHz 8N1）
#define WS2812_0_BIT 0b100   // 0ビット用信号
#define WS2812_1_BIT 0b110   // 1ビット用信号

/**
 * WS2812 LED driver class
 * Controls 3 systems of WS2812 LEDs using UART TX
 * Systems: P5_0, P5_3, P2_14
 */
class WS2812Driver {
public:
    /**
     * Constructor
     * Initializes UART TX for WS2812 control with port inversion
     */
    WS2812Driver();
    
    /**
     * Destructor
     */
    ~WS2812Driver();
    
    /**
     * Set color for specific LED in specific system
     * @param system System number (1-3)
     * @param led_id LED ID (1-256)
     * @param r Red value (0-255)
     * @param g Green value (0-255)
     * @param b Blue value (0-255)
     * @return true if successful, false otherwise
     */
    bool setColor(uint8_t system, uint8_t led_id, uint8_t r, uint8_t g, uint8_t b);
    
    /**
     * Set color for entire system
     * @param system System number (1-3)
     * @param r Red value (0-255)
     * @param g Green value (0-255)
     * @param b Blue value (0-255)
     * @return true if successful, false otherwise
     */
    bool setSystemColor(uint8_t system, uint8_t r, uint8_t g, uint8_t b);
    
    /**
     * Update WS2812 data for specific system
     * @param system System number (1-3)
     * @return true if successful, false otherwise
     */
    bool update(uint8_t system);
    
    /**
     * Update all WS2812 systems
     * @return true if successful, false otherwise
     */
    bool updateAll();
    
    /**
     * Turn off all LEDs in specific system
     * @param system System number (1-3)
     * @return true if successful, false otherwise
     */
    bool turnOff(uint8_t system);
    
    /**
     * Turn off all LEDs in all systems
     * @return true if successful, false otherwise
     */
    bool allOff();
    
    /**
     * Get current color of specific LED
     * @param system System number (1-3)
     * @param led_id LED ID (1-256)
     * @param r Pointer to store red value
     * @param g Pointer to store green value
     * @param b Pointer to store blue value
     * @return true if successful, false otherwise
     */
    bool getColor(uint8_t system, uint8_t led_id, uint8_t* r, uint8_t* g, uint8_t* b);

private:
    // UART TX for WS2812 control
    BufferedSerial _uart1;  // P5_0 - System 1
    BufferedSerial _uart2;  // P5_3 - System 2
    BufferedSerial _uart3;  // P2_14 - System 3
    
    // DMA for burst transfer (temporarily disabled)
    // DMAC _dma1;  // System 1 DMA
    // DMAC _dma2;  // System 2 DMA
    // DMAC _dma3;  // System 3 DMA
    
    // Color buffers for each system
    uint8_t _buffer1[WS2812_LED_COUNT * 24];  // 24 bits per LED
    uint8_t _buffer2[WS2812_LED_COUNT * 24];
    uint8_t _buffer3[WS2812_LED_COUNT * 24];
    
    // Current color data
    uint8_t _colors[WS2812_SYSTEMS][WS2812_LED_COUNT][3];  // [system][led][r,g,b]
    
    /**
     * Convert RGB color to WS2812 signal
     * @param r Red value (0-255)
     * @param g Green value (0-255)
     * @param b Blue value (0-255)
     * @param buffer Output buffer for WS2812 signal
     */
    void rgbToWS2812(uint8_t r, uint8_t g, uint8_t b, uint8_t* buffer);
    
    /**
     * Initialize port inversion for UART TX
     */
    void initPortInversion();
    
    /**
     * Initialize DMA for UART TX
     */
    void initDMA();
    
    /**
     * Send WS2812 data via UART
     * @param uart UART instance
     * @param buffer Data buffer
     * @param length Buffer length
     */
    void sendWS2812Data(BufferedSerial& uart, uint8_t* buffer, int length);
};

#endif // WS2812_DRIVER_H 