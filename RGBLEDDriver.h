#ifndef RGB_LED_DRIVER_H
#define RGB_LED_DRIVER_H

#include "mbed.h"

/**
 * RGB LED Driver Class
 * Provides functionality to control 3 RGB LED strips
 */
class RGBLEDDriver {
public:
    /**
     * Constructor
     * @param rgb1_r_pin RGB1 red pin number
     * @param rgb1_g_pin RGB1 green pin number
     * @param rgb1_b_pin RGB1 blue pin number
     * @param rgb2_r_pin RGB2 red pin number
     * @param rgb2_g_pin RGB2 green pin number
     * @param rgb2_b_pin RGB2 blue pin number
     * @param rgb3_r_pin RGB3 red pin number
     * @param rgb3_g_pin RGB3 green pin number
     * @param rgb3_b_pin RGB3 blue pin number
     */
    RGBLEDDriver(PinName rgb1_r_pin = P8_14, PinName rgb1_g_pin = P3_2, PinName rgb1_b_pin = P8_15,
                PinName rgb2_r_pin = P8_13, PinName rgb2_g_pin = P8_11, PinName rgb2_b_pin = P4_4,
                PinName rgb3_r_pin = P4_6, PinName rgb3_g_pin = P4_5, PinName rgb3_b_pin = P4_7);
    
    /**
     * Destructor
     */
    ~RGBLEDDriver();
    
    /**
     * Set the color of the specified RGB LED
     * @param id RGB LED number (1-3)
     * @param r Red intensity (0-255)
     * @param g Green intensity (0-255)
     * @param b Blue intensity (0-255)
     * @return true if successful, false if failed
     */
    bool setColor(uint8_t id, uint8_t r, uint8_t g, uint8_t b);
    
    /**
     * Turn off the specified RGB LED
     * @param id RGB LED number (1-3)
     * @return true if successful, false if failed
     */
    bool turnOff(uint8_t id);
    
    /**
     * Turn off all RGB LEDs
     */
    void allOff();
    
    /**
     * Get the current color of the specified RGB LED
     * @param id RGB LED number (1-3)
     * @param r Pointer to variable to store red intensity
     * @param g Pointer to variable to store green intensity
     * @param b Pointer to variable to store blue intensity
     * @return true if successful, false if failed
     */
    bool getColor(uint8_t id, uint8_t* r, uint8_t* g, uint8_t* b);
    
    /**
     * Set PWM period
     * @param period_us Period (microseconds)
     */
    void setPeriod(uint32_t period_us);

    /**
     * 色を滑らかに変化させる
     * @param id RGB LED number (1-3)
     * @param target_r 目標の赤色値 (0-255)
     * @param target_g 目標の緑色値 (0-255)
     * @param target_b 目標の青色値 (0-255)
     * @param transition_ms 変化にかける時間（ミリ秒）
     * @return true if successful, false if failed
     */
    bool setColorWithTransition(uint8_t id, uint8_t target_r, uint8_t target_g, uint8_t target_b, uint16_t transition_ms);

    /**
     * トランジションの更新処理
     * 定期的に呼び出す必要がある
     */
    void updateTransitions();

private:
    // RGB LED control PWM pins
    PwmOut* _rgb_pins[3][3]; // [LED number][color (R,G,B)]
    
    // Current color state
    uint8_t _colors[3][3]; // [LED number][color (R,G,B)]
    
    // PWM period (microseconds)
    uint32_t _period_us;
    
    // Default PWM period (20kHz)
    static const uint32_t DEFAULT_PERIOD_US = 50;

    // トランジション制御用の構造体
    struct Transition {
        bool active;
        uint8_t start_r, start_g, start_b;
        uint8_t target_r, target_g, target_b;
        uint32_t start_time;
        uint32_t duration_ms;
    };

    // 各LEDのトランジション状態
    Transition _transitions[3];

    // トランジションの更新間隔（ミリ秒）
    static const uint32_t TRANSITION_UPDATE_INTERVAL_MS = 20;  // 50Hz
};

#endif // RGB_LED_DRIVER_H 