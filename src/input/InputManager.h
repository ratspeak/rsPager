#pragma once

#include "hal/Keyboard.h"
#include "hal/Scrollwheel.h"
#include "hal/TouchInput.h"
#include "hal/Power.h"

class InputManager {
public:
    void begin(Keyboard* kb, Scrollwheel* sw, TouchInput* touch);
    void setPowerMgr(Power* pm) { _powerMgr = pm; }
    void setScrollwheelSpeed(uint8_t speed);
    void update();

    // Keyboard events
    bool hasKeyEvent() const { return _hasKey; }
    const KeyEvent& getKeyEvent() const { return _keyEvent; }

    // Any activity (for power wake)
    bool hadActivity() const { return _activity; }
    // Strong activity = keyboard/click (wakes from screen off)
    // Weak activity = encoder movement only (wakes from dim, NOT from screen off)
    bool hadStrongActivity() const { return _strongActivity; }

    // Long-press: encoder click held for >= threshold
    bool hadLongPress() const { return _longPress; }

private:
    Keyboard* _kb = nullptr;
    Scrollwheel* _sw = nullptr;
    TouchInput* _touch = nullptr;
    Power* _powerMgr = nullptr;

    bool _hasKey = false;
    KeyEvent _keyEvent;
    bool _activity = false;
    bool _strongActivity = false;
    bool _longPress = false;
    bool _clickPending = false;
    bool _longPressFired = false;
    bool _clickFromScreenOn = true;  // Captured at click DOWN to gate long-press
    unsigned long _clickStartMs = 0;
    unsigned long _lastClickDownMs = 0;
    static constexpr unsigned long LONG_PRESS_MS = 1200;
    static constexpr unsigned long CLICK_DEBOUNCE_MS = 80;

    // Encoder navigation state
    uint8_t _scrollwheelSpeed = 3;
    int8_t _swAccumX = 0;
    int8_t _swAccumY = 0;
    unsigned long _lastSwNavTime = 0;
    int8_t scrollwheelThreshold() const;
    unsigned long scrollwheelRateMs() const;

    // Touch polling throttle
    unsigned long _lastTouchPoll = 0;
    static constexpr unsigned long TOUCH_POLL_MS = 20;  // ~50Hz
};
