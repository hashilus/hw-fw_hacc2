#include "SSRDriver.h"
#include "mbed-os/targets/TARGET_RENESAS/TARGET_RZ_A1XX/TARGET_RZ_A1H/device/inc/iodefines/mtu2_iodefine.h"
#include "mbed-os/targets/TARGET_RENESAS/TARGET_RZ_A1XX/TARGET_RZ_A1H/device/inc/iobitmasks/mtu2_iobitmask.h"
#include "mbed.h"

// MTU0 TGRA interrupt number
#define MTU0_TGIA_IRQn  0x40  // MTU0 TGRA interrupt number

// Interrupt vector table
typedef void (*IRQHandler)(void);
IRQHandler g_interrupt_handlers[256] = {0};

// Global pointer to SSRDriver instance
static SSRDriver* g_ssr_driver = nullptr;

// MTU0 TGRA interrupt handler
extern "C" void MTU0_TGIA_IRQHandler(void) {
    if (g_ssr_driver) {
        g_ssr_driver->captureInterruptHandler();
    }
    // Clear interrupt flag
    MTU2.TSR_0 = 0x00;  // Clear TGFA bit
}

SSRDriver::SSRDriver(PinName ssr1, PinName ssr2, PinName ssr3, PinName ssr4)
    : _pwm1(ssr1), _pwm2(ssr2), _pwm3(ssr3), _pwm4(ssr4),
      _freq_timer() {
    // Pin configuration
    _ssr[0] = new DigitalOut(ssr1);
    _ssr[1] = new DigitalOut(ssr2);
    _ssr[2] = new DigitalOut(ssr3);
    _ssr[3] = new DigitalOut(ssr4);
    
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
    
    // Initialize frequency measurement
    _freq_measure.index = 0;
    _freq_measure.last_capture = 0;
    _freq_measure.frequency = 0;
    _freq_measure.buffer_full = false;
    
    // Initialize frequency measurement timer
    _freq_timer.start();
    
    // Configure MTU0 for input capture
    // Stop timer
    MTU2.TCR_0 = 0;
    
    // Set normal mode
    MTU2.TMDR_0 = MTU_TMDR_MD_NORMAL;
    
    // Configure input capture on rising edge
    MTU2.TIORH_0 = MTU_TIOR_IOA_INPUT;
    
    // Enable TGRA interrupt
    MTU2.TIER_0 = MTU_TIER_TGIEA;
    
    // Clear interrupt flags
    MTU2.TSR_0 = MTU_TSR_TGFA;
    
    // Set prescaler to PCLK/1
    MTU2.TCR_0 = 0x00;  // Set prescaler to PCLK/1
    
    // Register interrupt handler
    g_ssr_driver = this;
    
    // Start timer
    MTU2.TCR_0 |= 0x01;  // Start timer
    
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

uint32_t SSRDriver::getPowerLineFrequency() {
    return _freq_measure.frequency;
}

void SSRDriver::captureInterruptHandler() {
    uint32_t current_time = _freq_timer.read_us();
    
    if (_freq_measure.last_capture != 0) {
        uint32_t interval = current_time - _freq_measure.last_capture;
        _freq_measure.buffer[_freq_measure.index] = interval;
        _freq_measure.index++;
        
        if (_freq_measure.index >= 256) {
            _freq_measure.index = 0;
            _freq_measure.buffer_full = true;
            calculateFrequency();
        }
    }
    
    _freq_measure.last_capture = current_time;
}

void SSRDriver::calculateFrequency() {
    if (!_freq_measure.buffer_full) {
        return;
    }
    
    // Create a temporary buffer for sorting
    uint32_t temp_buffer[256];
    memcpy(temp_buffer, _freq_measure.buffer, sizeof(_freq_measure.buffer));
    
    // Sort the buffer (using insertion sort for simplicity)
    for (int i = 1; i < 256; i++) {
        uint32_t key = temp_buffer[i];
        int j = i - 1;
        
        while (j >= 0 && temp_buffer[j] > key) {
            temp_buffer[j + 1] = temp_buffer[j];
            j--;
        }
        temp_buffer[j + 1] = key;
    }
    
    // Calculate average excluding top and bottom 10%
    uint32_t sum = 0;
    for (int i = 26; i < 230; i++) {
        sum += temp_buffer[i];
    }
    
    // Calculate frequency (convert from microseconds to Hz)
    _freq_measure.frequency = 1000000 * 204 / sum;
} 