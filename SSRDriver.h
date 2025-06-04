#ifndef SSR_DRIVER_H
#define SSR_DRIVER_H

#include "mbed.h"

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
};

#endif // SSR_DRIVER_H 