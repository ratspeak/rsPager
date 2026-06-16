#pragma once

#include <Arduino.h>
#include "config/BoardConfig.h"

class Power {
public:
    // Enable peripheral rails through the XL9555 expander after I2C is online.
    static void enablePeripherals();

    void begin();
    void loop();

    // Strong activity = keyboard/click — wakes from any state
    void activity();
    // Weak activity = encoder movement — wakes from DIM only, not SCREEN_OFF
    void weakActivity();
    // Manual screen-off; no-op if activity() just woke from SCREEN_OFF this tick
    // so a single keypress can't both wake and re-sleep.
    void forceScreenOff();

    // True power off — callers flush state first. Enters BQ25896 ship mode
    // (BATFET open, ~26uA; wake = hold PWR ~1s or plug USB). On USB power SYS
    // never drops, so this parks in deep sleep with BOOT wake and the off
    // completes on unplug. Does not return.
    void powerOff();
    // One-shot: BOOT was short-pressed while the screen was on (sleep screen).
    bool screenSleepGestureFired();
    // One-shot: BOOT was held while the screen was on (show confirm UI).
    // Holding through POWEROFF_FORCE_MS powers off without UI — frozen-screen
    // escape hatch; skips state flush like a battery pull.
    bool powerOffGestureFired();
    // BQ25896 power-good (USB present)
    bool vbusPresent() const;

    // Battery
    float batteryVoltage() const;
    int batteryPercent() const;

    // Display backlight — accepts percentage 1-100
    void setBrightness(uint8_t percent);
    void setDimTimeout(uint16_t seconds) {
        _dimTimeout = seconds * 1000UL;
        if (_offTimeout > 0 && _offTimeout <= _dimTimeout) {
            _offTimeout = _dimTimeout + 10000UL;
        }
    }
    void setOffTimeout(uint16_t seconds) {
        _offTimeout = seconds * 1000UL;
        if (_offTimeout > 0 && _offTimeout <= _dimTimeout) {
            _offTimeout = _dimTimeout + 10000UL;
        }
    }

    // Keyboard backlight — accepts percentage 0-100 (0 = off)
    void setKbBrightness(uint8_t percent, bool apply=false);
    void setKbAutoOn(bool enable) { _kbAutoOn = enable; }
    void setKbAutoOff(bool enable) { _kbAutoOff = enable; }

    enum State { ACTIVE, DIMMED, SCREEN_OFF };
    State state() const { return _state; }
    bool isScreenOn() const { return _state != SCREEN_OFF; }
    bool isDimmed() const { return _state == DIMMED; }

private:
    void setState(State newState);
    uint8_t percentToPWM(uint8_t pct) const;
    void pollBootButton();
    static void disablePeripherals();

    static constexpr unsigned long POWEROFF_PROMPT_MS = 1200;
    static constexpr unsigned long POWEROFF_FORCE_MS = 6000;
    bool _btnWasDown = false;
    bool _btnFromScreenOn = false;
    bool _gestureLatched = false;
    bool _screenSleepPending = false;
    bool _gesturePending = false;
    unsigned long _btnDownMs = 0;

    State _state = ACTIVE;
    unsigned long _lastActivity = 0;
    unsigned long _dimTimeout = 30000;
    unsigned long _offTimeout = 60000;
    uint8_t _brightnessPct = 100;  // User brightness as 1-100%
    static constexpr uint8_t DIM_PWM = 40;  // ~15% PWM when dimmed
    bool _kbAutoOn = false;
    bool _kbAutoOff = false;
    bool _kbLitBeforeOff = false;
    bool _justWokeFromOff = false;
};
