#ifndef RGB_LED_DRIVER_H
#define RGB_LED_DRIVER_H

#include "mbed.h"
#include "SSRDriver.h"
#include "ConfigManager.h"

/**
 * RGB LED Driver Class
 * Provides functionality to control 3 RGB LED strips
 */
class RGBLEDDriver {
public:
    /**
     * Constructor
     * @param ssr_driver Reference to SSR driver
     * @param config_manager Pointer to config manager
     * @param rgb1_r_pin RGB1 red pin number
     * @param rgb1_g_pin RGB1 green pin number
     * @param rgb1_b_pin RGB1 blue pin number
     * @param rgb2_r_pin RGB2 red pin number
     * @param rgb2_g_pin RGB2 green pin number
     * @param rgb2_b_pin RGB2 blue pin number
     * @param rgb3_r_pin RGB3 red pin number
     * @param rgb3_g_pin RGB3 green pin number
     * @param rgb3_b_pin RGB3 blue pin number
     * @param rgb4_r_pin RGB4 red pin number
     * @param rgb4_g_pin RGB4 green pin number
     * @param rgb4_b_pin RGB4 blue pin number
     */
    RGBLEDDriver(SSRDriver& ssr_driver, ConfigManager* config_manager,
                PinName rgb1_r_pin = P8_14, PinName rgb1_g_pin = P3_2, PinName rgb1_b_pin = P8_15,
                PinName rgb2_r_pin = P8_13, PinName rgb2_g_pin = P8_11, PinName rgb2_b_pin = P4_4,
                PinName rgb3_r_pin = P4_6,  PinName rgb3_g_pin = P4_5,  PinName rgb3_b_pin = P4_7,
                PinName rgb4_r_pin = P3_10, PinName rgb4_g_pin = P3_8,  PinName rgb4_b_pin = P3_11);
    
    /**
     * Destructor
     */
    ~RGBLEDDriver();
    
    /**
     * Set the color of the specified RGB LED
     * @param id RGB LED number (1-4)
     * @param r Red intensity (0-255)
     * @param g Green intensity (0-255)
     * @param b Blue intensity (0-255)
     * @return true if successful, false if failed
     */
    bool setColor(uint8_t id, uint8_t r, uint8_t g, uint8_t b);
    
    /**
     * Turn off the specified RGB LED
     * @param id RGB LED number (1-4)
     * @return true if successful, false if failed
     */
    bool turnOff(uint8_t id);
    
    /**
     * Turn off all RGB LEDs
     */
    void allOff();
    
    /**
     * Get the current color of the specified RGB LED
     * @param id RGB LED number (1-4)
     * @param r Pointer to store red intensity (0-255)
     * @param g Pointer to store green intensity (0-255)
     * @param b Pointer to store blue intensity (0-255)
     * @return true if successful, false if failed
     */
    bool getColor(uint8_t id, uint8_t* r, uint8_t* g, uint8_t* b);
    
    /**
     * Set the PWM period for all RGB LEDs
     * @param period_us Period in microseconds
     */
    void setPeriod(uint32_t period_us);

    /**
     * 色を滑らかに変化させる
     * @param id RGB LED number (1-4)
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

    /**
     * Set the configuration manager
     * @param config_manager Pointer to the configuration manager
     */
    void setConfigManager(ConfigManager* config_manager) {
        _config_manager = config_manager;
    }

private:
    // RGB LED control PWM pins
    PwmOut* _rgb_pins[4][3]; // [LED number][color (R,G,B)]
    
    // Current color state
    uint8_t _colors[4][3]; // [LED number][color (R,G,B)]
    
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
    Transition _transitions[4];

    // トランジションの更新間隔（ミリ秒）
    static const uint32_t TRANSITION_UPDATE_INTERVAL_MS = 10;  // 100Hz

    // スレッド関連のメンバー
    Thread _transition_thread;
    bool _thread_running;
    EventQueue _event_queue;

    // SSRドライバーとコンフィグマネージャーの参照
    SSRDriver& _ssr_driver;
    ConfigManager* _config_manager;

    // トランジション更新用のスレッド関数
    void transitionThreadFunc();

    // SSRの状態に基づいてLEDの色を更新
    void updateSSRLinkColors();
};

#endif // RGB_LED_DRIVER_H 