#include "RGBLEDDriver.h"

RGBLEDDriver::RGBLEDDriver(PinName rgb1_r_pin, PinName rgb1_g_pin, PinName rgb1_b_pin,
                          PinName rgb2_r_pin, PinName rgb2_g_pin, PinName rgb2_b_pin,
                          PinName rgb3_r_pin, PinName rgb3_g_pin, PinName rgb3_b_pin) {
    // Initialize RGB LED pins
    _rgb_pins[0][0] = new PwmOut(rgb1_r_pin);
    _rgb_pins[0][1] = new PwmOut(rgb1_g_pin);
    _rgb_pins[0][2] = new PwmOut(rgb1_b_pin);
    
    _rgb_pins[1][0] = new PwmOut(rgb2_r_pin);
    _rgb_pins[1][1] = new PwmOut(rgb2_g_pin);
    _rgb_pins[1][2] = new PwmOut(rgb2_b_pin);
    
    _rgb_pins[2][0] = new PwmOut(rgb3_r_pin);
    _rgb_pins[2][1] = new PwmOut(rgb3_g_pin);
    _rgb_pins[2][2] = new PwmOut(rgb3_b_pin);
    
    // Initial settings
    _period_us = DEFAULT_PERIOD_US;
    
    // Initialize: turn off all LEDs and set PWM period
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            _rgb_pins[i][j]->period_us(_period_us);
            _rgb_pins[i][j]->write(0.0f);
            _colors[i][j] = 0;
        }
    }

    // トランジション状態の初期化
    for (int i = 0; i < 3; i++) {
        _transitions[i].active = false;
        _transitions[i].start_r = 0;
        _transitions[i].start_g = 0;
        _transitions[i].start_b = 0;
        _transitions[i].target_r = 0;
        _transitions[i].target_g = 0;
        _transitions[i].target_b = 0;
        _transitions[i].start_time = 0;
        _transitions[i].duration_ms = 0;
    }
}

RGBLEDDriver::~RGBLEDDriver() {
    // Free memory
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            delete _rgb_pins[i][j];
        }
    }
}

bool RGBLEDDriver::setColor(uint8_t id, uint8_t r, uint8_t g, uint8_t b) {
    // Check id
    if (id < 1 || id > 3) {
        return false;
    }
    
    // Convert to index
    uint8_t index = id - 1;
    
    // Save color values
    _colors[index][0] = r;
    _colors[index][1] = g;
    _colors[index][2] = b;
    
    // Set PWM output (convert 0-255 to 0.0-1.0)
    _rgb_pins[index][0]->write(r / 255.0f);
    _rgb_pins[index][1]->write(g / 255.0f);
    _rgb_pins[index][2]->write(b / 255.0f);
    
    return true;
}

bool RGBLEDDriver::turnOff(uint8_t id) {
    // Check id
    if (id < 1 || id > 3) {
        return false;
    }
    
    // Set color to 0 (turn off)
    return setColor(id, 0, 0, 0);
}

void RGBLEDDriver::allOff() {
    // Turn off all LEDs
    for (uint8_t i = 1; i <= 3; i++) {
        turnOff(i);
    }
}

bool RGBLEDDriver::getColor(uint8_t id, uint8_t* r, uint8_t* g, uint8_t* b) {
    // Check id
    if (id < 1 || id > 3) {
        return false;
    }
    
    // Check pointers
    if (r == nullptr || g == nullptr || b == nullptr) {
        return false;
    }
    
    // Convert to index
    uint8_t index = id - 1;
    
    // Return current color values
    *r = _colors[index][0];
    *g = _colors[index][1];
    *b = _colors[index][2];
    
    return true;
}

void RGBLEDDriver::setPeriod(uint32_t period_us) {
    // Update PWM period
    _period_us = period_us;
    
    // Set new period for all PWM pins
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            _rgb_pins[i][j]->period_us(_period_us);
        }
    }
}

bool RGBLEDDriver::setColorWithTransition(uint8_t id, uint8_t target_r, uint8_t target_g, uint8_t target_b, uint16_t transition_ms) {
    // Check id
    if (id < 1 || id > 3) {
        return false;
    }
    
    // Convert to index
    uint8_t index = id - 1;
    
    // 現在の色を取得
    uint8_t current_r, current_g, current_b;
    getColor(id, &current_r, &current_g, &current_b);
    
    // トランジション状態を設定
    _transitions[index].active = true;
    _transitions[index].start_r = current_r;
    _transitions[index].start_g = current_g;
    _transitions[index].start_b = current_b;
    _transitions[index].target_r = target_r;
    _transitions[index].target_g = target_g;
    _transitions[index].target_b = target_b;
    _transitions[index].start_time = us_ticker_read() / 1000;  // 現在時刻（ミリ秒）
    _transitions[index].duration_ms = transition_ms;
    
    return true;
}

void RGBLEDDriver::updateTransitions() {
    uint32_t current_time = us_ticker_read() / 1000;  // 現在時刻（ミリ秒）
    
    for (int i = 0; i < 3; i++) {
        if (!_transitions[i].active) continue;
        
        Transition& t = _transitions[i];
        uint32_t elapsed = current_time - t.start_time;
        
        if (elapsed >= t.duration_ms) {
            // トランジション完了
            setColor(i + 1, t.target_r, t.target_g, t.target_b);
            t.active = false;
        } else {
            // トランジション中
            float progress = (float)elapsed / t.duration_ms;
            uint8_t r = t.start_r + (t.target_r - t.start_r) * progress;
            uint8_t g = t.start_g + (t.target_g - t.start_g) * progress;
            uint8_t b = t.start_b + (t.target_b - t.start_b) * progress;
            setColor(i + 1, r, g, b);
        }
    }
} 