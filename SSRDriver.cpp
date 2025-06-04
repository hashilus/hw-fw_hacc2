#include "SSRDriver.h"

SSRDriver::SSRDriver(PinName ssr1_pin, PinName ssr2_pin, PinName ssr3_pin, PinName ssr4_pin) {
    // Pin configuration
    _ssr[0] = new DigitalOut(ssr1_pin);
    _ssr[1] = new DigitalOut(ssr2_pin);
    _ssr[2] = new DigitalOut(ssr3_pin);
    _ssr[3] = new DigitalOut(ssr4_pin);
    
    // Initialize
    for (int i = 0; i < 4; i++) {
        _ssr[i]->write(0);
        _state[i] = false;
        _duty_level[i] = 0;
        _is_pwm[i] = false;
    }
    
    // Initialize PWM control counter
    _pwm_counter = 0;
    
    // Set default PWM frequency to 1Hz
    _pwm_frequency_hz = 1;
    _pwm_period_ms = 1000; // 1Hz = 1000ms
    
    // Start timer
    updateTimer();
}

SSRDriver::~SSRDriver() {
    // Stop timer
    _pwm_ticker.detach();
    
    // Free memory
    for (int i = 0; i < 4; i++) {
        delete _ssr[i];
    }
}

bool SSRDriver::turnOn(uint8_t id) {
    // Check id
    if (id < 1 || id > 4) {
        return false;
    }
    
    // Convert to index
    uint8_t index = id - 1;
    
    // If PWM mode, set duty cycle to 100 (maximum)
    if (_is_pwm[index]) {
        return setDutyLevel(id, 100);
    }
    
    // If digital output mode, turn ON
    _ssr[index]->write(1);
    _state[index] = true;
    
    return true;
}

bool SSRDriver::turnOff(uint8_t id) {
    // Check id
    if (id < 1 || id > 4) {
        return false;
    }
    
    // Convert to index
    uint8_t index = id - 1;
    
    // If PWM mode, set duty cycle to 0
    if (_is_pwm[index]) {
        return setDutyLevel(id, 0);
    }
    
    // If digital output mode, turn OFF
    _ssr[index]->write(0);
    _state[index] = false;
    
    return true;
}

bool SSRDriver::getState(uint8_t id) {
    // Check id
    if (id < 1 || id > 4) {
        return false;
    }
    
    // Convert to index
    uint8_t index = id - 1;
    
    // Return current state
    return _state[index];
}

void SSRDriver::allOff() {
    // Turn off all SSRs
    for (int i = 0; i < 4; i++) {
        _ssr[i]->write(0);
        _state[i] = false;
        _duty_level[i] = 0;
    }
}

bool SSRDriver::setDutyLevel(uint8_t id, uint8_t level) {
    // Check id
    if (id < 1 || id > 4) {
        return false;
    }
    
    // Check level (0-100)
    if (level > 100) {
        level = 100;
    }
    
    // Convert to index
    uint8_t index = id - 1;
    
    // If PWM mode is disabled, only ON/OFF is available
    if (!_is_pwm[index]) {
        if (level == 0) {
            _ssr[index]->write(0);
            _state[index] = false;
        } else {
            _ssr[index]->write(1);
            _state[index] = true;
        }
        _duty_level[index] = level;
        return true;
    }
    
    // Set duty cycle level
    _duty_level[index] = level;
    
    // Update state
    _state[index] = (level > 0);
    
    return true;
}

uint8_t SSRDriver::getDutyLevel(uint8_t id) {
    // Check id
    if (id < 1 || id > 4) {
        return 0;
    }
    
    // Convert to index
    uint8_t index = id - 1;
    
    // Return current duty cycle level
    return _duty_level[index];
}

bool SSRDriver::enablePWM(uint8_t id, bool enable) {
    // Check id
    if (id < 1 || id > 4) {
        return false;
    }
    
    // Convert to index
    uint8_t index = id - 1;
    
    // If current state is the same, do nothing
    if (_is_pwm[index] == enable) {
        return true;
    }
    
    // Switch PWM mode
    _is_pwm[index] = enable;
    
    return true;
}

bool SSRDriver::setPWMFrequency(uint8_t frequency_hz) {
    // Check frequency range (1-10Hz)
    if (frequency_hz < 1 || frequency_hz > 10) {
        return false;
    }
    
    // Update frequency and period
    _pwm_frequency_hz = frequency_hz;
    _pwm_period_ms = 1000 / frequency_hz; // Period (ms) = 1000 / Frequency (Hz)
    
    // Reset PWM counter
    _pwm_counter = 0;
    
    // Update timer
    updateTimer();
    
    return true;
}

uint8_t SSRDriver::getPWMFrequency() {
    return _pwm_frequency_hz;
}

void SSRDriver::updateTimer() {
    // Stop current timer
    _pwm_ticker.detach();
    
    // Restart timer (call callback at 1/resolution intervals of PWM period)
    _pwm_ticker.attach(callback(this, &SSRDriver::timerCallback), 
                      std::chrono::milliseconds(_pwm_period_ms / PWM_RESOLUTION));
}

void SSRDriver::timerCallback() {
    // PWM counter update
    _pwm_counter = (_pwm_counter + 1) % PWM_RESOLUTION;
    
    // PWM control for each SSR
    for (int i = 0; i < 4; i++) {
        if (_is_pwm[i]) {
            // If counter is less than duty cycle level, turn ON, otherwise turn OFF
            if (_pwm_counter < _duty_level[i]) {
                _ssr[i]->write(1);
            } else {
                _ssr[i]->write(0);
            }
        }
    }
} 