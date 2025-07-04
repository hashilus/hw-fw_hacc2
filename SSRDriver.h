#ifndef SSR_DRIVER_H
#define SSR_DRIVER_H

#include "mbed.h"

// MTU0レジスタ定義
#define MTU0_BASE           (0xE8000000UL)
#define MTU0_TCR           (*(volatile uint16_t *)(MTU0_BASE + 0x00))
#define MTU0_TMDR          (*(volatile uint16_t *)(MTU0_BASE + 0x02))
#define MTU0_TIORH         (*(volatile uint16_t *)(MTU0_BASE + 0x04))
#define MTU0_TIORL         (*(volatile uint16_t *)(MTU0_BASE + 0x06))
#define MTU0_TIER          (*(volatile uint16_t *)(MTU0_BASE + 0x08))
#define MTU0_TSR           (*(volatile uint16_t *)(MTU0_BASE + 0x0A))
#define MTU0_TCNT          (*(volatile uint16_t *)(MTU0_BASE + 0x0C))
#define MTU0_TGRA          (*(volatile uint16_t *)(MTU0_BASE + 0x0E))
#define MTU0_TGRB          (*(volatile uint16_t *)(MTU0_BASE + 0x10))
#define MTU0_TGRC          (*(volatile uint16_t *)(MTU0_BASE + 0x12))
#define MTU0_TGRD          (*(volatile uint16_t *)(MTU0_BASE + 0x14))

// MTU0ビット定義
#define MTU_TCR_TPSC_1     (0x00)  // PCLK/1
#define MTU_TCR_TPSC_4     (0x01)  // PCLK/4
#define MTU_TCR_TPSC_16    (0x02)  // PCLK/16
#define MTU_TCR_TPSC_64    (0x03)  // PCLK/64
#define MTU_TCR_TPSC_256   (0x04)  // PCLK/256
#define MTU_TCR_TPSC_1024  (0x05)  // PCLK/1024

#define MTU_TMDR_MD_NORMAL (0x00)  // 通常動作モード

#define MTU_TIOR_IOA_INPUT (0x01)  // 入力キャプチャ（立ち上がりエッジ）

#define MTU_TIER_TGIEA     (0x01)  // TGRA割り込み許可

#define MTU_TSR_TGFA       (0x01)  // TGRAフラグ

/**
 * Solid State Relay (SSR) driver class
 * Provides functionality to control 4 SSRs
 */
class SSRDriver {
public:
    /**
     * Constructor
     * @param ssr1_pin Pin number for SSR1
     * @param ssr2_pin Pin number for SSR2
     * @param ssr3_pin Pin number for SSR3
     * @param ssr4_pin Pin number for SSR4
     */
    SSRDriver(PinName ssr1_pin = P4_0, PinName ssr2_pin = P2_13, PinName ssr3_pin = P5_7, PinName ssr4_pin = P5_6);
    
    /**
     * Destructor
     */
    ~SSRDriver();
    
    /**
     * Turn ON the SSR
     * @param id SSR number (1-4)
     * @return true if successful, false otherwise
     */
    bool turnOn(uint8_t id);
    
    /**
     * Turn OFF the SSR
     * @param id SSR number (1-4)
     * @return true if successful, false otherwise
     */
    bool turnOff(uint8_t id);
    
    /**
     * Get the state of the SSR
     * @param id SSR number (1-4)
     * @return true if ON, false if OFF
     */
    bool getState(uint8_t id);
    
    /**
     * Turn OFF all SSRs
     */
    void allOff();
    
    /**
     * Set the duty cycle for the SSR (software PWM mode)
     * @param id SSR number (1-4)
     * @param level Duty cycle level (0-100), 0: fully OFF, 100: fully ON
     * @return true if successful, false otherwise
     */
    bool setDutyLevel(uint8_t id, uint8_t level);
    
    /**
     * Get the current duty cycle level of the SSR
     * @param id SSR number (1-4)
     * @return Duty cycle level (0-100)
     */
    uint8_t getDutyLevel(uint8_t id);
    
    /**
     * Enable PWM mode
     * @param id SSR number (1-4)
     * @param enable true for PWM mode, false for digital output mode
     * @return true if successful, false otherwise
     */
    bool enablePWM(uint8_t id, bool enable);
    
    /**
     * Set PWM frequency (common for all SSRs)
     * @param frequency_hz Frequency (1-10Hz)
     * @return true if successful, false otherwise
     */
    bool setPWMFrequency(uint8_t frequency_hz);
    
    /**
     * Get the current PWM frequency
     * @return Current PWM frequency (Hz)
     */
    uint8_t getPWMFrequency();
    
    /**
     * Timer callback for PWM control
     */
    void timerCallback();

    /**
     * Get the current power line frequency
     * @return Current power line frequency (Hz)
     */
    uint32_t getPowerLineFrequency();

    void captureInterruptHandler();

private:
    DigitalOut* _ssr[4];   // Pins for SSR control
    bool _state[4];        // Current state
    uint8_t _duty_level[4]; // Current duty cycle level (0-100)
    bool _is_pwm[4];       // Whether PWM mode is enabled
    
    // Timer for PWM control
    Ticker _pwm_ticker;
    
    // Counter for PWM control
    uint8_t _pwm_counter;
    
    // PWM period (ms)
    uint32_t _pwm_period_ms;
    
    // PWM frequency (Hz)
    uint8_t _pwm_frequency_hz;
    
    // PWM resolution
    const uint8_t PWM_RESOLUTION = 100; // 100 steps
    
    // Update timer
    void updateTimer();

    // Power line frequency measurement
    struct {
        uint32_t buffer[256];  // Measurement buffer
        uint8_t index;         // Current buffer position
        uint32_t last_capture; // Last capture value
        uint32_t frequency;    // Calculated frequency
        bool buffer_full;      // Whether buffer is full
    } _freq_measure;

    // Timer for frequency measurement
    Timer _freq_timer;
    
    // Calculate frequency from measurements
    void calculateFrequency();

    PwmOut _pwm1;
    PwmOut _pwm2;
    PwmOut _pwm3;
    PwmOut _pwm4;
};

#endif // SSR_DRIVER_H 