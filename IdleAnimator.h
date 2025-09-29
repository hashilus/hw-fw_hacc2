#ifndef IDLE_ANIMATOR_H
#define IDLE_ANIMATOR_H

#include "mbed.h"
#include "RGBLEDDriver.h"

/**
 * 一定時間アクティビティが無い場合に、RGB LED 1,2,3 を
 * ランダムな色・ランダムな間隔で切り替えるアイドルアニメータ。
 * LED4 は対象外。
 */
class IdleAnimator {
public:
    explicit IdleAnimator(RGBLEDDriver* rgb_driver);
    ~IdleAnimator();

    void start();
    void stop();
    void notifyActivity();

    void setIdleTimeout(std::chrono::milliseconds timeout_ms);
    void setIntervalRange(std::chrono::milliseconds min_ms, std::chrono::milliseconds max_ms);
    void setFadeDuration(std::chrono::milliseconds fade_ms);

    bool isActive() const { return _idle_active; }

private:
    // 設定
    std::chrono::milliseconds _idle_timeout_ms;
    std::chrono::milliseconds _min_interval_ms;
    std::chrono::milliseconds _max_interval_ms;
    std::chrono::milliseconds _fade_ms;

    // 参照
    RGBLEDDriver* _rgb;

    // 状態
    bool _running;
    volatile bool _idle_active;

    // 実行基盤
    events::EventQueue _queue;
    rtos::Thread _thread;
    Timeout _idle_timeout;
    Timeout _next_change_timeout;

    // 内部処理
    void armIdleTimer();
    void onIdleTimeoutISR();
    void scheduleNextChangeISR();
    void onChangeTimeoutISR();

    void onIdleBegin();
    void onChangeColor();
    void pickBrightRandom(uint8_t& r, uint8_t& g, uint8_t& b);
    void hsvToRgb(float h, float s, float v, uint8_t& r, uint8_t& g, uint8_t& b);
};

#endif // IDLE_ANIMATOR_H


