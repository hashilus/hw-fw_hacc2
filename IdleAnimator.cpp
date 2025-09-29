#include "IdleAnimator.h"
#include <cstdlib>

IdleAnimator::IdleAnimator(RGBLEDDriver* rgb_driver)
    : _idle_timeout_ms(5s)
    , _min_interval_ms(800ms)
    , _max_interval_ms(3000ms)
    , _fade_ms(600ms)
    , _rgb(rgb_driver)
    , _running(false)
    , _idle_active(false) {
}

IdleAnimator::~IdleAnimator() {
    stop();
}

void IdleAnimator::start() {
    if (_running) return;
    _running = true;
    std::srand((unsigned)us_ticker_read());
    _thread.start(callback(&_queue, &events::EventQueue::dispatch_forever));
    notifyActivity();
}

void IdleAnimator::stop() {
    _idle_timeout.detach();
    _next_change_timeout.detach();
    _idle_active = false;
    if (_running) {
        _queue.break_dispatch();
        if (_thread.get_state() == rtos::Thread::Running) {
            _thread.join();
        }
        _running = false;
    }
}

void IdleAnimator::notifyActivity() {
    _idle_active = false;
    _next_change_timeout.detach();
    armIdleTimer();
}

void IdleAnimator::setIdleTimeout(std::chrono::milliseconds timeout_ms) {
    _idle_timeout_ms = timeout_ms;
}

void IdleAnimator::setIntervalRange(std::chrono::milliseconds min_ms, std::chrono::milliseconds max_ms) {
    _min_interval_ms = min_ms;
    _max_interval_ms = max_ms;
}

void IdleAnimator::setFadeDuration(std::chrono::milliseconds fade_ms) {
    _fade_ms = fade_ms;
}

void IdleAnimator::armIdleTimer() {
    _idle_timeout.detach();
    // 0は無効: タイマーを張らず、アイドル状態を解除して終了
    if (_idle_timeout_ms.count() == 0) {
        _idle_active = false;
        _next_change_timeout.detach();
        return;
    }
    _idle_timeout.attach(callback(this, &IdleAnimator::onIdleTimeoutISR), _idle_timeout_ms);
}

void IdleAnimator::onIdleTimeoutISR() {
    _queue.call(callback(this, &IdleAnimator::onIdleBegin));
}

void IdleAnimator::scheduleNextChangeISR() {
    // 乱数で次の間隔を決める
    uint32_t span = _max_interval_ms.count() - _min_interval_ms.count();
    uint32_t jitter = span ? (std::rand() % (span + 1)) : 0;
    auto next_ms = _min_interval_ms + std::chrono::milliseconds(jitter);
    _next_change_timeout.attach(callback(this, &IdleAnimator::onChangeTimeoutISR), next_ms);
}

void IdleAnimator::onIdleBegin() {
    _idle_active = true;
    onChangeColor();
}

void IdleAnimator::onChangeColor() {
    if (!_idle_active || !_rgb) {
        return;
    }

    // LED 1,2,3 を独立して更新（LED4は対象外）
    for (int id = 1; id <= 3; ++id) {
        uint8_t r, g, b;
        pickBrightRandom(r, g, b);
        if (_fade_ms.count() > 0) {
            _rgb->setColorWithTransition(id, r, g, b, (uint16_t)_fade_ms.count());
        } else {
            _rgb->setColor(id, r, g, b);
        }
    }

    // 次回変更をスケジュール
    scheduleNextChangeISR();
}

void IdleAnimator::onChangeTimeoutISR() {
    _queue.call(callback(this, &IdleAnimator::onChangeColor));
}

void IdleAnimator::pickBrightRandom(uint8_t& r, uint8_t& g, uint8_t& b) {
    // 暗め禁止: Vは0.6〜1.0、Sは0.6〜1.0、Hは0〜360
    float h = (std::rand() % 36000) / 100.0f; // 0.00〜360.00
    float s = 0.60f + ((std::rand() % 4000) / 10000.0f); // 0.60〜1.00
    float v = 0.70f + ((std::rand() % 3000) / 10000.0f); // 0.70〜1.00 さらに明るめに
    if (s > 1.0f) s = 1.0f;
    if (v > 1.0f) v = 1.0f;
    hsvToRgb(h, s, v, r, g, b);
}

void IdleAnimator::hsvToRgb(float h, float s, float v, uint8_t& r, uint8_t& g, uint8_t& b) {
    float c = v * s;
    float x = c * (1 - fabsf(fmodf(h / 60.0f, 2.0f) - 1));
    float m = v - c;
    float rp = 0, gp = 0, bp = 0;
    if (h < 60)      { rp = c; gp = x; bp = 0; }
    else if (h < 120){ rp = x; gp = c; bp = 0; }
    else if (h < 180){ rp = 0; gp = c; bp = x; }
    else if (h < 240){ rp = 0; gp = x; bp = c; }
    else if (h < 300){ rp = x; gp = 0; bp = c; }
    else             { rp = c; gp = 0; bp = x; }
    r = (uint8_t)((rp + m) * 255.0f + 0.5f);
    g = (uint8_t)((gp + m) * 255.0f + 0.5f);
    b = (uint8_t)((bp + m) * 255.0f + 0.5f);
}


