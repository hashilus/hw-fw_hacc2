#include "RGBLEDDriver.h"
#include "SerialController.h"

RGBLEDDriver::RGBLEDDriver(SSRDriver& ssr_driver, ConfigManager* config_manager,
                          PinName rgb1_r_pin, PinName rgb1_g_pin, PinName rgb1_b_pin,
                          PinName rgb2_r_pin, PinName rgb2_g_pin, PinName rgb2_b_pin,
                          PinName rgb3_r_pin, PinName rgb3_g_pin, PinName rgb3_b_pin,
                          PinName rgb4_r_pin, PinName rgb4_g_pin, PinName rgb4_b_pin)
    : _thread_running(false), _ssr_driver(ssr_driver), _config_manager(config_manager) {
    
    log_printf(LOG_LEVEL_DEBUG, "[DEBUG] RGBLEDDriver: Starting initialization");
    ThisThread::sleep_for(10ms);  // 出力完了を待つ
    
    // Initialize RGB LED pins
    log_printf(LOG_LEVEL_DEBUG, "[DEBUG] RGBLEDDriver: Creating PWM pins for LED1");
    ThisThread::sleep_for(5ms);  // 出力完了を待つ
    _rgb_pins[0][0] = new PwmOut(rgb1_r_pin);
    _rgb_pins[0][1] = new PwmOut(rgb1_g_pin);
    _rgb_pins[0][2] = new PwmOut(rgb1_b_pin);
    
    log_printf(LOG_LEVEL_DEBUG, "[DEBUG] RGBLEDDriver: Creating PWM pins for LED2");
    ThisThread::sleep_for(5ms);  // 出力完了を待つ
    _rgb_pins[1][0] = new PwmOut(rgb2_r_pin);
    _rgb_pins[1][1] = new PwmOut(rgb2_g_pin);
    _rgb_pins[1][2] = new PwmOut(rgb2_b_pin);
    
    log_printf(LOG_LEVEL_DEBUG, "[DEBUG] RGBLEDDriver: Creating PWM pins for LED3");
    ThisThread::sleep_for(5ms);  // 出力完了を待つ
    _rgb_pins[2][0] = new PwmOut(rgb3_r_pin);
    _rgb_pins[2][1] = new PwmOut(rgb3_g_pin);
    _rgb_pins[2][2] = new PwmOut(rgb3_b_pin);
    
    log_printf(LOG_LEVEL_DEBUG, "[DEBUG] RGBLEDDriver: Creating PWM pins for LED4");
    ThisThread::sleep_for(5ms);  // 出力完了を待つ
    _rgb_pins[3][0] = new PwmOut(rgb4_r_pin);
    _rgb_pins[3][1] = new PwmOut(rgb4_g_pin);
    _rgb_pins[3][2] = new PwmOut(rgb4_b_pin);
    
    log_printf(LOG_LEVEL_DEBUG, "[DEBUG] RGBLEDDriver: Setting initial PWM period");
    ThisThread::sleep_for(5ms);  // 出力完了を待つ
    // Initial settings
    _period_us = DEFAULT_PERIOD_US;
    
    // Initialize: turn off all LEDs and set PWM period
    log_printf(LOG_LEVEL_DEBUG, "[DEBUG] RGBLEDDriver: Configuring PWM pins");
    ThisThread::sleep_for(10ms);  // 出力完了を待つ
    
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 3; j++) {
            log_printf(LOG_LEVEL_DEBUG, "[DEBUG] RGBLEDDriver: Setting PWM period for LED%d, color%d", i+1, j+1);
            ThisThread::sleep_for(5ms);  // 出力完了を待つ
            _rgb_pins[i][j]->period_us(_period_us);
            log_printf(LOG_LEVEL_DEBUG, "[DEBUG] RGBLEDDriver: PWM period set for LED%d, color%d", i+1, j+1);
            ThisThread::sleep_for(5ms);  // 出力完了を待つ
            
            log_printf(LOG_LEVEL_DEBUG, "[DEBUG] RGBLEDDriver: Setting PWM duty to 0 for LED%d, color%d", i+1, j+1);
            ThisThread::sleep_for(5ms);  // 出力完了を待つ
            _rgb_pins[i][j]->write(0.0f);
            log_printf(LOG_LEVEL_DEBUG, "[DEBUG] RGBLEDDriver: PWM duty set for LED%d, color%d", i+1, j+1);
            ThisThread::sleep_for(5ms);  // 出力完了を待つ
            
            _colors[i][j] = 0;
        }
    }

    log_printf(LOG_LEVEL_DEBUG, "[DEBUG] RGBLEDDriver: Initializing transition states");
    ThisThread::sleep_for(5ms);  // 出力完了を待つ
    // トランジション状態の初期化
    for (int i = 0; i < 4; i++) {
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

    log_printf(LOG_LEVEL_DEBUG, "[DEBUG] RGBLEDDriver: Starting transition thread");
    ThisThread::sleep_for(5ms);  // 出力完了を待つ
    // トランジション更新スレッドを開始
    _thread_running = true;
    _transition_thread.start(callback(this, &RGBLEDDriver::transitionThreadFunc));
    
    log_printf(LOG_LEVEL_DEBUG, "[DEBUG] RGBLEDDriver: Initialization completed");
    ThisThread::sleep_for(10ms);  // 出力完了を待つ
}

RGBLEDDriver::~RGBLEDDriver() {
    // スレッドを停止
    _thread_running = false;
    if (_transition_thread.get_state() == Thread::Running) {
        _transition_thread.join();
    }

    // Free memory
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 3; j++) {
            delete _rgb_pins[i][j];
        }
    }
}

bool RGBLEDDriver::setColor(uint8_t id, uint8_t r, uint8_t g, uint8_t b) {
    // Check id
    if (id < 1 || id > 4) {
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
    if (id < 1 || id > 4) {
        return false;
    }
    
    // Set color to 0 (turn off)
    return setColor(id, 0, 0, 0);
}

void RGBLEDDriver::allOff() {
    // Turn off all LEDs
    for (uint8_t i = 1; i <= 4; i++) {
        turnOff(i);
    }
}

bool RGBLEDDriver::getColor(uint8_t id, uint8_t* r, uint8_t* g, uint8_t* b) {
    // Check id
    if (id < 1 || id > 4) {
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
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 3; j++) {
            _rgb_pins[i][j]->period_us(_period_us);
        }
    }
}

bool RGBLEDDriver::setColorWithTransition(uint8_t id, uint8_t target_r, uint8_t target_g, uint8_t target_b, uint16_t transition_ms) {
    // Check id
    if (id < 1 || id > 4) {
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

void RGBLEDDriver::updateSSRLinkColors() {
    static uint32_t last_status_time = 0;  // 前回のステータス表示時刻
    uint32_t current_time = us_ticker_read() / 1000;  // 現在時刻（ミリ秒）

    if (!_config_manager || !_config_manager->isSSRLinkEnabled()) {
        return;
    }

    // SSR1（チャンネル1）のデューティ比を取得
    static int last_duty = -1;  // 前回のデューティ比を保存
    int duty = _ssr_driver.getDutyLevel(1);
    
    // デューティ比が変更された場合のみ更新
    if (duty != last_duty) {
        log_printf(LOG_LEVEL_DEBUG, "[DEBUG] SSR1 duty level changed: %d%% -> %d%%", last_duty, duty);
        ThisThread::sleep_for(5ms);  // 出力完了を待つ
        last_duty = duty;
        
        // 各RGB LEDに対して、SSR1の出力に応じて色を更新
        for (int i = 1; i <= 4; i++) {
            // 0%と100%の色を取得
            RGBColorData color0 = _config_manager->getSSRLinkColor0(i);
            RGBColorData color100 = _config_manager->getSSRLinkColor100(i);
            
            log_printf(LOG_LEVEL_DEBUG, "[DEBUG] LED%d: Color0=(%d,%d,%d), Color100=(%d,%d,%d)", 
                   i, color0.r, color0.g, color0.b, color100.r, color100.g, color100.b);
            ThisThread::sleep_for(5ms);  // 出力完了を待つ
            
            // デューティ比に応じて色を補間
            uint8_t r = color0.r + (color100.r - color0.r) * duty / 100;
            uint8_t g = color0.g + (color100.g - color0.g) * duty / 100;
            uint8_t b = color0.b + (color100.b - color0.b) * duty / 100;
            
            log_printf(LOG_LEVEL_DEBUG, "[DEBUG] LED%d: Calculated color=(%d,%d,%d)", i, r, g, b);
            ThisThread::sleep_for(5ms);  // 出力完了を待つ
            
            // トランジション付きで色を設定
            setColorWithTransition(i, r, g, b, _config_manager->getSSRLinkTransitionTime());
        }
    }
}

void RGBLEDDriver::transitionThreadFunc() {
    while (_thread_running) {
        uint32_t current_time = us_ticker_read() / 1000;  // 現在時刻（ミリ秒）
        
        // トランジションの更新
        for (int i = 0; i < 4; i++) {
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

        // SSR-LEDリンクの更新
        updateSSRLinkColors();
        
        // 更新間隔を待機
        ThisThread::sleep_for(TRANSITION_UPDATE_INTERVAL_MS);
    }
} 