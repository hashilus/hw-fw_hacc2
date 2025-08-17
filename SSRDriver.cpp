#include "SSRDriver.h"
#include "mbed.h"

SSRDriver::SSRDriver(PinName ssr1, PinName ssr2, PinName ssr3, PinName ssr4)
{
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
        _ssr_start_time[i] = 0;
        _pwm_frequency_hz_individual[i] = 1; // デフォルトは1Hz
    }
    
    // Initialize time-based control counters
    for (int i = 0; i < 4; i++) {
        _ssr_counter[i] = 0;
        _ssr_period[i] = 0;
        _time_on_count[i] = 0;
    }

    // ゼロクロス検出用InterruptIn初期化（立ち上がりエッジのみ、プルアップ設定）
    _zerox_in.rise(callback(this, &SSRDriver::zeroxEdgeHandler));
    _zerox_in.mode(PullUp);  // P3_9をプルアップ設定
    _zerox_timer.start();
    _last_rise_time_us = 0;   // 初期化
    
    // 電源周波数計算用履歴の初期化
    for (int i = 0; i < FREQ_HISTORY_SIZE; i++) {
        _zerox_timestamps[i] = 0;
    }
    _zerox_history_index = 0;
    
    // 半波整流制御用変数の初期化
    _alternate_control = false;  // 最初は即座実行

    // ゼロクロス検出の割り込み優先度を最高レベルに設定
    // RZ_A1HのGPIO割り込み優先度を0（最高優先度）に設定
    // ゼロクロスピン（P3_9）の割り込み優先度を最高に設定
    GIC_SetPriority((IRQn_Type)(0x40 + 9), 0);  // P3_9の割り込み番号は0x40 + 9、優先度0（最高）
    
    // 他の重要な割り込みの優先度を調整してゼロクロス割り込みを最優先に
    // システムタイマー割り込みの優先度を下げる（ゼロクロスより低く）
    // RZ_A1HではSysTick_IRQnの代わりに適切な割り込み番号を使用
    // GIC_SetPriority(SysTick_IRQn, 2);  // システムタイマー割り込み優先度を2に設定
}

SSRDriver::~SSRDriver() {
    // Stop triac timeouts
    for (int i = 0; i < 4; i++) {
        _triac_off_timeout[i].detach();
    }
    
    // Stop zero-cross control timeout
    _zerox_control_timeout.detach();
    
    // Free memory
    for (int i = 0; i < 4; i++) {
        delete _ssr[i];
    }
    
    // P8_11入力端子のメモリを解放
    if (_p8_11_input) {
        delete _p8_11_input;
    }
}

bool SSRDriver::turnOn(uint8_t id) {
    // Check id
    if (id < 1 || id > 4) {
        return false;
    }
    
    // ゼロクロス制御でON（次のゼロクロスで反映）
    return setDutyLevel(id - 1, 100);
}

bool SSRDriver::turnOff(uint8_t id) {
    // Check id
    if (id < 1 || id > 4) {
        return false;
    }
    
    // ゼロクロス制御でOFF（次のゼロクロスで反映）
    return setDutyLevel(id - 1, 0);
}

bool SSRDriver::getState(uint8_t id) {
    // Check id
    if (id < 1 || id > 4) {
        return false;
    }
    
    // Return current state
    return _state[id - 1];
}

void SSRDriver::allOff() {
    // Turn off all SSRs
    for (int i = 0; i < 4; i++) {
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
    
    uint8_t index = id - 1;
    _duty_level[index] = level;
    
    _time_on_count[index] = (_ssr_period[index] * level) / 100;
    
    return true;
}

uint8_t SSRDriver::getDutyLevel(uint8_t id) {
    // Check id
    if (id < 1 || id > 4) {
        return 0;
    }
    
    // Return current duty cycle level
    return _duty_level[id - 1];
}

bool SSRDriver::setPWMFrequency(int8_t frequency_hz) {
    if (frequency_hz < -1 || frequency_hz > 10) {
        return false;
    }
    
    // -1設定時の設定変更無効化ロジック
    bool has_minus_one = false;
    for (int i = 0; i < 4; i++) {
        if (_pwm_frequency_hz_individual[i] == -1) {
            has_minus_one = true;
            break;
        }
    }
    
    if (has_minus_one && frequency_hz != 0) {
        // -1が設定されているチャンネルがある場合は、0以外の設定変更を無効化
        return false;
    }
    
    _pwm_frequency_hz = frequency_hz;
    for (int i = 1; i <= 4; i++) {
        setPWMFrequency(i, frequency_hz);
    }
    return true;
}

int8_t SSRDriver::getPWMFrequency() {
    return _pwm_frequency_hz;
}

bool SSRDriver::getSSRStatus(uint8_t id, uint8_t& duty_level, bool& state, uint32_t& period) {
    // Check id
    if (id < 1 || id > 4) {
        return false;
    }
    
    uint8_t index = id - 1;
    duty_level = _duty_level[index];
    state = _state[index];
    period = _ssr_period[index];
    
    return true;
}

bool SSRDriver::setPWMFrequency(uint8_t id, int8_t frequency_hz) {
    // Check id
    if (id < 1 || id > 4) {
        return false;
    }
    
    // Check frequency range
    if (frequency_hz < -1 || frequency_hz > 10) {
        return false;
    }
    
    uint8_t index = id - 1;
    
    // -1設定時の設定変更無効化ロジック
    if (_pwm_frequency_hz_individual[index] == -1 && frequency_hz != 0) {
        // -1が設定されている場合は、0以外の設定変更を無効化
        return false;
    }
    
    _pwm_frequency_hz_individual[index] = frequency_hz;
    
    // Update time-based control parameters for this channel
    if (frequency_hz == -1 || frequency_hz == 0) {
        _ssr_period[index] = 0;  // ゼロクロス同期制御
        _ssr_counter[index] = 0;  // カウンタをリセット
    } else {
        // 時間周期での制御（ゼロクロス回数単位）
        // 割り込みで測定した周波数を使用
        float power_freq = getPowerLineFrequency();
        uint32_t zerox_per_second = (uint32_t)(power_freq + 0.5f);  // 四捨五入して整数に変換
        
        // 測定値が無効な場合はデフォルト値を使用
        if (zerox_per_second < 50 || zerox_per_second > 70) {
            zerox_per_second = 60;  // 50Hz〜70Hzの範囲外はデフォルト
        }
        
        _ssr_period[index] = zerox_per_second / frequency_hz * 2;
        _time_on_count[index] = (_ssr_period[index] * _duty_level[index]) / 100;
        _ssr_counter[index] = 0;  // カウンタをリセット
    }
    
    return true;
}

int8_t SSRDriver::getPWMFrequency(uint8_t id) {
    // Check id
    if (id < 1 || id > 4) {
        return 0;
    }
    
    uint8_t index = id - 1;
    return _pwm_frequency_hz_individual[index];
}

// updateTimer()とtimerCallback()は削除（Ticker不要のため）

float SSRDriver::getPowerLineFrequency() const {
    // 過去100回の割り込みから電源周波数を算出
    uint32_t valid_samples = 0;
    uint32_t total_interval_us = 0;
    
    // 履歴から有効な間隔を計算
    for (int i = 0; i < FREQ_HISTORY_SIZE; i++) {
        uint32_t current_index = (_zerox_history_index - 1 - i + FREQ_HISTORY_SIZE) % FREQ_HISTORY_SIZE;
        uint32_t prev_index = (_zerox_history_index - 2 - i + FREQ_HISTORY_SIZE) % FREQ_HISTORY_SIZE;
        
        // 有効な時刻データがある場合のみ計算
        if (_zerox_timestamps[current_index] > 0 && _zerox_timestamps[prev_index] > 0) {
            uint32_t interval = _zerox_timestamps[current_index] - _zerox_timestamps[prev_index];
            
            // 有効な間隔（50Hz=20ms〜60Hz=16.67msの範囲）
            if (interval >= 15000 && interval <= 25000) {  // 15ms〜25ms
                total_interval_us += interval;
                valid_samples++;
            }
        }
    }
    
    // 有効なサンプルが十分にある場合のみ計算
    if (valid_samples >= 10) {  // 最低10サンプル必要
        float avg_interval_us = (float)total_interval_us / valid_samples;
        float power_frequency = 1000000.0f / avg_interval_us;  // 電源周波数
        
        // 50Hz/60Hzの判定（ノイズ対策）
        if (power_frequency >= 45.0f && power_frequency <= 65.0f) {
            return power_frequency;  // float型で返す
        }
    }
    
    // 測定値が無効な場合はデフォルト値を返す
    return 60.0f;
}

 

// ゼロクロス割り込みハンドラ（立ち上がりエッジのみ）
void SSRDriver::zeroxEdgeHandler() {
    // 割り込みが禁止されている場合は処理をスキップ
    if (_interrupt_disabled) {
        return;
    }
    
    uint32_t now = _zerox_timer.elapsed_time().count();
    
    // 割り込み時刻を履歴に記録（過去100回の割り込みから電源周波数を算出）
    _zerox_timestamps[_zerox_history_index] = now;
    _zerox_history_index = (_zerox_history_index + 1) % FREQ_HISTORY_SIZE;
    
    // ゼロクロス統計を更新
    _zerox_flag = true;
    _zerox_count++;
    
    // 前回の立ち上がりエッジ時刻を記録
    _last_rise_time_us = now;
    
    // 割り込みを15msec間禁止
    _interrupt_disabled = true;
    _interrupt_enable_timeout.attach(callback(this, &SSRDriver::enableInterruptHandler), 
                                   std::chrono::milliseconds(15));
    
    // 即座実行：zeroxControlHandlerを即座に実行
    zeroxControlHandler();

    // 遅延実行：電源周波数の半分の時間後にzeroxControlHandlerを実行
    float power_freq = getPowerLineFrequency();
    if (power_freq < 1.0f) {
        power_freq = 60.0f;  // デフォルト値
    }
    uint32_t half_cycle_us = (uint32_t)(1000000.0f / power_freq / 2.0f + 0.5f);  // 半周期時間
    
    _delayed_control_timeout.attach(callback(this, &SSRDriver::delayedControlHandler), 
                                    std::chrono::microseconds(half_cycle_us));
}

// 割り込み再有効化ハンドラ（15msec後に呼び出し）
void SSRDriver::enableInterruptHandler() {
    _interrupt_disabled = false;
}

// 遅延制御ハンドラ（電源周波数の半分の時間後に呼び出し）
void SSRDriver::delayedControlHandler() {
    zeroxControlHandler();
}

// ゼロクロス制御ハンドラ（Tickerで呼び出し）
void SSRDriver::zeroxControlHandler() {
    // SSR制御（すべてゼロクロスに同期）
    for (int i = 0; i < 4; i++) {
        if (_ssr_period[i] == 0) {
            if (_duty_level[i] <= 0) {
                _ssr[i]->write(0);
                _state[i] = false;
            } else if (_duty_level[i] >= 100) {
                _ssr[i]->write(1);
                _state[i] = true;
            } else {
                // 商用電源周波数を自動検出してデューティ比を計算
                float power_freq = getPowerLineFrequency();
                if (power_freq < 1.0f) {
                    power_freq = 60.0f;  // 60Hz地域のデフォルト
                }
                // // 60Hz地域での手動補正（必要に応じて）
                // if (power_freq < 55.0f) {
                //     power_freq = 60.0f;  // 50Hzとして誤検出された場合の補正
                // }
                
                // デューティ比に応じたONタイミングを計算
                // ゼロクロス検出は両エッジなので、周期は電源周波数の倍
                uint32_t zero_cross_freq = power_freq * 2;  // 50Hz → 100Hz, 60Hz → 120Hz
                uint32_t cycle_time_us = 1000000 / zero_cross_freq;  // 例：100Hz = 10000μs
                
                // デューティ比に応じたONタイミング計算
                // 0〜100%を20〜85%に変換
                float duty_ratio;
                if (_duty_level[i] <= 0) {
                    // 0%の場合は20%に変換
                    duty_ratio = 0.20f;
                } else if (_duty_level[i] >= 100) {
                    // 100%の場合は85%に変換
                    duty_ratio = 0.85f;
                } else {
                    // 1〜99%を20〜85%に線形変換
                    // 変換式: 20 + (元の値 / 100) * (85 - 20) = 20 + 元の値 * 0.65
                    duty_ratio = (20.0f + (_duty_level[i] * 0.65f)) / 100.0f;
                }
                
                // 数値が低いほど1サイクル時間後に、数値が高いほどゼロクロス直後にON
                uint32_t on_delay_us = (uint32_t)(cycle_time_us * (1.0f - duty_ratio) + 0.5f);  // 四捨五入
                
                // ON時間は1msec固定
                uint32_t on_time_us = 1000;  // 1msec = 1000μs固定
                
                // デバッグ変数を更新（割り込み内で軽量処理）
                _debug_power_freq = power_freq;
                _debug_on_time_us = on_time_us;
                _debug_cycle_time_us = cycle_time_us;
                
                _triac_off_timeout[i].detach();
                
                // デューティ比に応じた遅延時間後にON（トライアック制御）
                switch(i) {
                    case 0: _triac_off_timeout[i].attach(callback(this, &SSRDriver::turnOnSSR0), std::chrono::microseconds(on_delay_us)); break;
                    case 1: _triac_off_timeout[i].attach(callback(this, &SSRDriver::turnOnSSR1), std::chrono::microseconds(on_delay_us)); break;
                    case 2: _triac_off_timeout[i].attach(callback(this, &SSRDriver::turnOnSSR2), std::chrono::microseconds(on_delay_us)); break;
                    case 3: _triac_off_timeout[i].attach(callback(this, &SSRDriver::turnOnSSR3), std::chrono::microseconds(on_delay_us)); break;
                }
            }
            // デューティ比0%と100%の場合はsetDutyLevelで即座に制御済み
        } else {
            // 時間周期での制御（カウンタベース）
            // カウンタを加算
            _ssr_counter[i]++;
            
            // 周期を超えたらカウンタをリセット
            if (_ssr_counter[i] >= _ssr_period[i]) {
                _ssr_counter[i] = 0;
            }
            
            // デューティ比に基づいてON/OFF決定
            bool should_be_on = (_ssr_counter[i] < _time_on_count[i]);
            
            // 現在の状態と異なる場合のみ変更（ノイズ対策）
            if (should_be_on != _state[i]) {
                _ssr[i]->write(should_be_on ? 1 : 0);
                _state[i] = should_be_on;
            }
        }
    }
}

// トライアックON用コールバック（遅延ON + 1msec後にOFF）
void SSRDriver::turnOnSSR0() {
    _ssr[0]->write(1);
    _state[0] = true;
    // 1msec後にOFF
    _triac_off_timeout[0].attach(callback(this, &SSRDriver::turnOffSSR0), std::chrono::microseconds(1000));
}

// トライアックON用コールバック（遅延ON + 1msec後にOFF）
void SSRDriver::turnOnSSR1() {
    _ssr[1]->write(1);
    _state[1] = true;
    // 1msec後にOFF
    _triac_off_timeout[1].attach(callback(this, &SSRDriver::turnOffSSR1), std::chrono::microseconds(1000));
}

// トライアックON用コールバック（遅延ON + 1msec後にOFF）
void SSRDriver::turnOnSSR2() {
    _ssr[2]->write(1);
    _state[2] = true;
    // 1msec後にOFF
    _triac_off_timeout[2].attach(callback(this, &SSRDriver::turnOffSSR2), std::chrono::microseconds(1000));
}

// トライアックON用コールバック（遅延ON + 1msec後にOFF）
void SSRDriver::turnOnSSR3() {
    _ssr[3]->write(1);
    _state[3] = true;
    // 1msec後にOFF
    _triac_off_timeout[3].attach(callback(this, &SSRDriver::turnOffSSR3), std::chrono::microseconds(1000));
}

// トライアックOFF用コールバック
void SSRDriver::turnOffSSR0() {
    _ssr[0]->write(0);
    _state[0] = false;
}

// トライアックOFF用コールバック
void SSRDriver::turnOffSSR1() {
    _ssr[1]->write(0);
    _state[1] = false;
}

// トライアックOFF用コールバック
void SSRDriver::turnOffSSR2() {
    _ssr[2]->write(0);
    _state[2] = false;
}

// トライアックOFF用コールバック
void SSRDriver::turnOffSSR3() {
    _ssr[3]->write(0);
    _state[3] = false;
}

// SSR制御の内部状態を更新（周期カウント等）
void SSRDriver::updateControl() {
    // ゼロクロス割り込みで制御するため、ここでは何もしない
    // この関数は後方互換性のために残す
}

void SSRDriver::getZeroCrossStats(uint32_t& count, uint32_t& interval, float& frequency) const {
    count = _zerox_count;
    
    // 最新の間隔を計算
    interval = 0;
    frequency = 0.0f;
    
    if (_zerox_history_index > 0) {
        uint32_t current_index = (_zerox_history_index - 1 + FREQ_HISTORY_SIZE) % FREQ_HISTORY_SIZE;
        uint32_t prev_index = (_zerox_history_index - 2 + FREQ_HISTORY_SIZE) % FREQ_HISTORY_SIZE;
        
        if (_zerox_timestamps[current_index] > 0 && _zerox_timestamps[prev_index] > 0) {
            interval = _zerox_timestamps[current_index] - _zerox_timestamps[prev_index];
        }
    }
    
    // 周波数は100回の平均値（getPowerLineFrequencyを使用）
    frequency = getPowerLineFrequency();
}

 