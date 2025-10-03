#ifndef WS2812_DRIVER_H
#define WS2812_DRIVER_H

#include "mbed.h"

// WS2812制御用定数
#define WS2812_LED_COUNT 256  // 各系統のLED数
#define WS2812_SYSTEMS 3     // 系統数

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
    // SPI for WS2812 control (1系統=1本のMOSI)
    SPI _spi0;  // SPI0: MOSI=P10_14, SCLK=P10_12
    SPI _spi1;  // SPI1: MOSI=P11_14, SCLK=P11_12
    SPI _spi3;  // SPI3: MOSI=P5_2,  SCLK=P5_0
    
    // 残置配線対策: 旧UARTピンを入力に設定（高インピーダンス）
    DigitalIn _in_p5_3; // was UART3 TX
    DigitalIn _in_p2_14; // was UART0 TX
    
    // DMA for burst transfer (temporarily disabled)
    // DMAC _dma1;  // System 1 DMA
    // DMAC _dma2;  // System 2 DMA
    // DMAC _dma3;  // System 3 DMA
    
    // Encoded byte buffers for each system (9 bytes per LED)
    uint8_t _buffer0[WS2812_LED_COUNT * 9];
    uint8_t _buffer1[WS2812_LED_COUNT * 9];
    uint8_t _buffer3_buf[WS2812_LED_COUNT * 9];
    
    // Current color data
    uint8_t _colors[WS2812_SYSTEMS][WS2812_LED_COUNT][3];  // [system][led][r,g,b]
    
    /**
     * Encode one LED's GRB to SPI byte stream (9 bytes per LED)
     * @param r Red value (0-255)
     * @param g Green value (0-255)
     * @param b Blue value (0-255)
     * @param out9 Output buffer (size >= 9)
     */
    void encodeGRBToSPI(uint8_t r, uint8_t g, uint8_t b, uint8_t* out9);

    /** Encode one byte (MSB first) to 24 WS2812 bits (3 bytes) using 0->100,1->110 */
    static void encodeByteTo24Bits(uint8_t value, uint8_t* out3);
    
    /**
     * Send WS2812 data via SPI
     * @param spi SPI instance
     * @param buffer Data buffer
     * @param length Buffer length
     */
    void sendWS2812Data(SPI& spi, const uint8_t* buffer, int length);
};

#endif // WS2812_DRIVER_H 