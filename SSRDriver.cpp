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
        _pwm_frequency_hz_individual[i] = 0; // デフォルトはゼロクロス同期制御
    }
    
    // Set default PWM frequency to 1Hz
    _pwm_frequency_hz = 1;
    
    // Initialize time-based control counters
    for (int i = 0; i < 4; i++) {
        _ssr_counter[i] = 0;
        _ssr_period[i] = 0;
        _time_on_count[i] = 0;
    }

    // ゼロクロス検出用InterruptIn初期化（両エッジ検出）
    _zerox_in.rise(callback(this, &SSRDriver::zeroxEdgeHandler));
    _zerox_in.fall(callback(this, &SSRDriver::zeroxEdgeHandler));
    _zerox_timer.start();
    _last_rise_time_us = 0;   // 初期化
    _zerox_delay_us = 0;      // 初期化

    // ゼロクロス検出の割り込み優先度を最高レベルに設定
    // RZ_A1HのGPIO割り込み優先度を0（最高優先度）に変更
    // ゼロクロスピン（P3_9）の割り込み優先度を最高に設定
    GIC_SetPriority((IRQn_Type)(0x40 + 9), 1);  // P3_9の割り込み番号は0x40 + 9
    
    // P8_11ピンを汎用GPIO入力端子として初期化（将来の拡張用）
    _p8_11_input = new DigitalIn(P8_11, PullUp);
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
    
    // デューティ比0%と100%の場合は即座に制御（ゼロクロス待ちなし）
    if (_ssr_period[index] == 0) {  // ゼロクロス同期制御の場合
        if (level == 0) {
            // デューティ比0%: 即座にOFF
            _triac_off_timeout[index].detach();
            _ssr[index]->write(0);
            _state[index] = false;
        } else if (level == 100) {
            // デューティ比100%: 即座にON
            _triac_off_timeout[index].detach();
            _ssr[index]->write(1);
            _state[index] = true;
        } else {
            // デューティ比1-99%の場合はゼロクロス割り込みで制御
            // 既存のタイマーをクリアして、次のゼロクロスで制御開始
            _triac_off_timeout[index].detach();
        }
    }
    
    // 時間周期制御の場合、ON時間を再計算
    if (_ssr_period[index] > 0) {
        _time_on_count[index] = (_ssr_period[index] * level) / 100;
    }
    
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

bool SSRDriver::setPWMFrequency(uint8_t frequency_hz) {
    if (frequency_hz > 10) {
        return false;
    }
    _pwm_frequency_hz = frequency_hz;
    for (int i = 1; i <= 4; i++) {
        setPWMFrequency(i, frequency_hz);
    }
    return true;
}

uint8_t SSRDriver::getPWMFrequency() {
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

bool SSRDriver::setPWMFrequency(uint8_t id, uint8_t frequency_hz) {
    // Check id
    if (id < 1 || id > 4) {
        return false;
    }
    
    // Check frequency range
    if (frequency_hz > 10) {
        return false;
    }
    
    uint8_t index = id - 1;
    _pwm_frequency_hz_individual[index] = frequency_hz;
    
    // Update time-based control parameters for this channel
    if (frequency_hz == 0) {
        _ssr_period[index] = 0;  // ゼロクロス同期制御
        _ssr_counter[index] = 0;  // カウンタをリセット
    } else {
        // 時間周期での制御（ゼロクロス回数単位）
        // 割り込みで測定した周波数を使用
        uint32_t power_freq = getPowerLineFrequency();
        uint32_t zerox_per_second = power_freq * 2;  // 両エッジ検出なので2倍
        
        // 測定値が無効な場合はデフォルト値を使用
        if (zerox_per_second < 100 || zerox_per_second > 140) {
            zerox_per_second = 120;  // 50Hz(100Hz)〜70Hz(140Hz)の範囲外はデフォルト
        }
        
        _ssr_period[index] = zerox_per_second / frequency_hz;
        _time_on_count[index] = (_ssr_period[index] * _duty_level[index]) / 100;
        _ssr_counter[index] = 0;  // カウンタをリセット
    }
    
    return true;
}

uint8_t SSRDriver::getPWMFrequency(uint8_t id) {
    // Check id
    if (id < 1 || id > 4) {
        return 0;
    }
    
    uint8_t index = id - 1;
    return _pwm_frequency_hz_individual[index];
}

// updateTimer()とtimerCallback()は削除（Ticker不要のため）

uint32_t SSRDriver::getPowerLineFrequency() {
    // ゼロクロス割り込みで測定した周波数を返す
    if (_last_zerox_interval_us > 0) {
        // 両エッジ検出なので、電源周波数の2倍の周波数で検出される
        // 例：60Hz電源 → 120Hz検出 → 8333μs間隔
        float detected_frequency = 1000000.0f / _last_zerox_interval_us;  // 検出周波数
        float power_frequency = detected_frequency / 2.0f;  // 電源周波数
        
        // 50Hz/60Hzの判定（ノイズ対策）
        if (power_frequency >= 45.0f && power_frequency <= 65.0f) {
            return (uint32_t)(power_frequency + 0.5f);  // 四捨五入
        }
    }
    
    // 測定値が無効な場合はデフォルト値を返す
    return 60;
}

 

// ゼロクロス割り込みハンドラ（両エッジ検出）
void SSRDriver::zeroxEdgeHandler() {
    uint32_t now = _zerox_timer.elapsed_time().count();
    bool is_rising = _zerox_in.read();  // 現在のピン状態を読み取り
    
    if (is_rising) {
        // 立ち上がりエッジ：現在時刻を保存
        _rise_time_us = now;
        _waiting_for_fall = true;
        
        // 前回の立ち上がりエッジ時刻が記録済みの場合、立ち上がりエッジ間隔を計算
        if (_last_rise_time_us > 0) {
            // 立ち上がりエッジ間隔 = 現在時刻 - 前回の立ち上がりエッジ時刻
            _last_zerox_interval_us = now - _last_rise_time_us;
            
            // ゼロクロス統計を更新
            _zerox_flag = true;
            _zerox_count++;
        }
        
        // 現在の立ち上がりエッジ時刻を記録
        _last_rise_time_us = now;
        
        // 前回の遅延時間が計算済みの場合、次のゼロクロス割り込みを設定
        if (_zerox_delay_us > 0) {
            // 計算された遅延時間後にゼロクロス割り込みを設定
            _zerox_control_timeout.attach(callback(this, &SSRDriver::zeroxControlHandler), 
                                        std::chrono::microseconds(_zerox_delay_us));
        }
    } else {
        // 立ち下がりエッジ：ゼロクロス遅延時間を計算
        if (_waiting_for_fall) {
            _fall_time_us = now;
            _waiting_for_fall = false;
            
            // ゼロクロス遅延時間を計算：インターバル時間 - ((立ち下がり時刻 - 立ち上がり時刻) / 2)
            if (_rise_time_us > 0 && _fall_time_us > _rise_time_us && _last_zerox_interval_us > 0) {
                uint32_t wave_width = _fall_time_us - _rise_time_us;
                _zerox_delay_us = _last_zerox_interval_us - (uint32_t)(wave_width * 0.5f);
            }
        }
    }
}

// ゼロクロス制御ハンドラ（Tickerで呼び出し）
void SSRDriver::zeroxControlHandler() {
    // SSR制御（すべてゼロクロスに同期）
    for (int i = 0; i < 4; i++) {
        if (_ssr_period[i] == 0) {
            // トライアック動作：1サイクル内でのON時間を可変制御
            // デューティ比0%と100%の場合は即座に制御済みなのでスキップ
            if (_duty_level[i] > 0 && _duty_level[i] < 100) {
                // 商用電源周波数を自動検出してデューティ比を計算
                uint32_t power_freq = getPowerLineFrequency();
                if (power_freq == 0) {
                    power_freq = 60;  // 60Hz地域のデフォルト
                }
                // 60Hz地域での手動補正（必要に応じて）
                if (power_freq < 55) {
                    power_freq = 60;  // 50Hzとして誤検出された場合の補正
                }
                
                // デューティ比に応じたONタイミングを計算
                // ゼロクロス検出は両エッジなので、周期は電源周波数の倍
                uint32_t zero_cross_freq = power_freq * 2;  // 50Hz → 100Hz, 60Hz → 120Hz
                uint32_t cycle_time_us = 1000000 / zero_cross_freq;  // 例：100Hz = 10000μs
                
                // デューティ比に応じたONタイミング計算
                // 0〜99%を0〜80%に変換（100%は変換しない）
                float duty_ratio;
                if (_duty_level[i] < 100) {
                    // 0〜99%を0〜80%に変換: 変換後 = 元の値 × 0.80
                    duty_ratio = (_duty_level[i] * 0.80f) / 100.0f;
                } else {
                    // 100%は変換しない
                    duty_ratio = 1.0f;
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
    interval = _last_zerox_interval_us;  // 立ち上がりエッジ間隔（マイクロ秒）
    frequency = 0.0f;
    
    if (interval > 0) {
        frequency = 1000000.0f / interval;  // 周波数 = 1,000,000 / 間隔(μs)
    }
}

 