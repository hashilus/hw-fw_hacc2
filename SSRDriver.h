#ifndef SSR_DRIVER_H
#define SSR_DRIVER_H

#include "mbed.h"

// ゼロクロス検出ピン
#define ZEROX_PIN P3_9



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
     * Set PWM frequency (common for all SSRs)
     * @param frequency_hz Frequency (1-10Hz)
     * @return true if successful, false otherwise
     */
    bool setPWMFrequency(uint8_t frequency_hz);
    
    /**
     * Set PWM frequency for specific SSR channel
     * @param id SSR number (1-4)
     * @param frequency_hz Frequency (1-10Hz)
     * @return true if successful, false otherwise
     */
    bool setPWMFrequency(uint8_t id, uint8_t frequency_hz);
    
    /**
     * Get the current PWM frequency
     * @return Current PWM frequency (Hz)
     */
    uint8_t getPWMFrequency();
    
    /**
     * Get detailed status of SSR for debugging
     * @param id SSR number (1-4)
     * @param duty_level Output parameter for duty level
     * @param state Output parameter for current state
     * @param period Output parameter for control period
     * @return true if successful, false otherwise
     */
    bool getSSRStatus(uint8_t id, uint8_t& duty_level, bool& state, uint32_t& period);
    
    /**
     * Get the current PWM frequency for specific SSR channel
     * @param id SSR number (1-4)
     * @return Current PWM frequency (Hz)
     */
    uint8_t getPWMFrequency(uint8_t id);
    
    /**
     * Get zero-cross detection status
     * @return true if zero-cross is detected, false otherwise
     */
    bool isZeroCrossDetected() const { return _zerox_flag; }
    
    /**
     * Get zero-cross interval
     * @return Interval between zero-crosses in microseconds
     */
    uint32_t getZeroCrossInterval() const { 
        // 最新の間隔を計算
        if (_zerox_history_index > 0) {
            uint32_t current_index = (_zerox_history_index - 1 + FREQ_HISTORY_SIZE) % FREQ_HISTORY_SIZE;
            uint32_t prev_index = (_zerox_history_index - 2 + FREQ_HISTORY_SIZE) % FREQ_HISTORY_SIZE;
            
            if (_zerox_timestamps[current_index] > 0 && _zerox_timestamps[prev_index] > 0) {
                return _zerox_timestamps[current_index] - _zerox_timestamps[prev_index];
            }
        }
        return 0;  // 立ち上がりエッジ間隔（マイクロ秒）
    }
    
    /**
     * Get zero-cross count (for debugging)
     * @return Number of zero-cross detections
     */
    uint32_t getZeroCrossCount() const { return _zerox_count; }
    
    /**
     * Reset zero-cross count and get current statistics
     * @return Current count before reset
     */
    uint32_t resetZeroCrossCount() { 
        uint32_t count = _zerox_count; 
        _zerox_count = 0; 
        return count; 
    }
    
    /**
     * Get zero-cross statistics for monitoring
     * @param count Output: number of detections
     * @param interval Output: last interval in microseconds
     * @param frequency Output: calculated frequency in Hz
     */
    void getZeroCrossStats(uint32_t& count, uint32_t& interval, float& frequency) const;
    
    // デバッグ情報取得
    void getDebugInfo(uint32_t& power_freq, uint32_t& on_time_us, uint32_t& cycle_time_us) const;
    
    /**
     * Timer callback for PWM control
     */
    // timerCallback()は削除（Ticker不要のため）

    /**
     * Get the current power line frequency
     * @return Current power line frequency (Hz)
     */
    float getPowerLineFrequency() const;



    /**
     * ゼロクロス割り込みハンドラ（立ち上がりエッジのみ）
     */
    void zeroxEdgeHandler();
    
    /**
     * ゼロクロス制御ハンドラ（Tickerで呼び出し）
     */
    void zeroxControlHandler();
    
    /**
     * 割り込み再有効化ハンドラ（15msec後に呼び出し）
     */
    void enableInterruptHandler();
    
    /**
     * 遅延制御ハンドラ（電源周波数の半分の時間後に呼び出し）
     */
    void delayedControlHandler();

    /**
     * SSR制御の内部状態を更新（周期カウント等）
     * 注意：現在はゼロクロス割り込みで制御するため、この関数は何もしません
     */
    void updateControl();

private:
    DigitalOut* _ssr[4];   // Pins for SSR control
    bool _state[4];        // Current state
    uint8_t _duty_level[4]; // Current duty cycle level (0-100)
    
    // PWM frequency (Hz) - common for all SSRs
    uint8_t _pwm_frequency_hz;
    
    // PWM frequency (Hz) - individual for each SSR
    uint8_t _pwm_frequency_hz_individual[4];
    
    // 時間周期制御用カウンタ（ゼロクロス割り込み内で加算）
    uint32_t _time_on_count[4] = {0}; // 各チャンネルのON時間（ゼロクロス回数）



    // ゼロクロス検出用（立ち上がりエッジのみ）
    InterruptIn _zerox_in{ZEROX_PIN};
    volatile bool _zerox_flag = false;
    Timer _zerox_timer; // ゼロクロス周期計測用
    uint32_t _zerox_count = 0;  // ゼロクロス検出回数（デバッグ用）
    
    // 割り込み禁止制御用
    volatile bool _interrupt_disabled = false;  // 割り込み禁止フラグ
    Timeout _interrupt_enable_timeout;  // 割り込み再有効化用Timeout
    
    // 半波整流制御用
    volatile bool _alternate_control = false;  // 交互制御フラグ（false=即座実行、true=遅延実行）
    Timeout _delayed_control_timeout;  // 遅延制御用Timeout
    
    // P8_11入力端子（将来の拡張用）
    DigitalIn* _p8_11_input;
    
    // 電源周波数計算用（過去100回の割り込みから算出）
    static const uint8_t FREQ_HISTORY_SIZE = 100;  // 履歴サイズ
    uint32_t _zerox_timestamps[FREQ_HISTORY_SIZE];  // 割り込み時刻の履歴
    uint8_t _zerox_history_index = 0;  // 履歴インデックス
    uint32_t _last_rise_time_us = 0;   // 最後の立ち上がりエッジ時刻
    
    // デバッグ用変数（割り込み内で更新、メインループで出力）
    volatile float _debug_power_freq = 0.0f;  // 検出された商用電源周波数
    volatile uint32_t _debug_on_time_us = 0;  // 計算されたON時間
    volatile uint32_t _debug_cycle_time_us = 0;  // 計算された周期時間
    
    // トライアック制御用Timeout
    Timeout _triac_off_timeout[4];  // OFF用のTimeout
    Timeout _zerox_control_timeout;  // ゼロクロス制御用Timeout

    // SSR制御用カウンタ（統一後）
    uint32_t _ssr_counter[4] = {0};  // 各チャンネルのカウンタ（ゼロクロス回数）
    uint32_t _ssr_period[4] = {0};   // 各チャンネルの周期（ゼロクロス回数）
    uint32_t _ssr_start_time[4] = {0}; // 各SSRの周期開始時刻（ms）- 後方互換性のため残す
    // トライアック制御用
    uint32_t _triac_delay_us = 100; // ゼロクロスからONまでの遅延時間（マイクロ秒）
    
    // トライアックON用コールバック
    void turnOnSSR(int ssr_id);
    // トライアックOFF用コールバック
    void turnOffSSR(int ssr_id);
    // トライアック制御用コールバック（各SSR専用）
    void turnOnSSR0();
    void turnOnSSR1();
    void turnOnSSR2();
    void turnOnSSR3();
    void turnOffSSR0();
    void turnOffSSR1();
    void turnOffSSR2();
    void turnOffSSR3();
};

#endif // SSR_DRIVER_H 