#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "config/BoardConfig.h"

// Input modes
enum class InputMode {
    Navigation,  // Arrow-like movement, hotkeys active
    TextInput    // Character entry, Esc exits to Navigation
};

// Simplified key event for consumers
struct KeyEvent {
    char character;
    bool ctrl;
    bool shift;
    bool fn;
    bool alt;
    bool opt;
    bool enter;
    bool del;
    bool tab;
    bool space;
    // Directional arrows (from encoder/LVGL navigation)
    bool up;
    bool down;
    bool left;
    bool right;
    // True when this event came from the T-Pager scroll wheel/click.
    bool encoder;
    // Synthesized auto-repeat while a key is held (backspace only).
    // Handlers using backspace as back/dismiss navigation must ignore these.
    bool repeat;
};

class Keyboard {
public:
    bool begin();
    void update();

    // Mode control
    InputMode getMode() const { return _mode; }
    void setMode(InputMode mode) { _mode = mode; }

    // State queries
    bool hasEvent() const { return _hasEvent; }
    const KeyEvent& getEvent() const { return _event; }

    // Backlight control
    bool setBacklightBrightness(uint8_t percent); // 0 stores off; non-zero doesn't change current brightness
    bool backlightOn();
    bool backlightOff();
    bool backlightIsLit() const { return _backlightLit; }

private:
    uint8_t readRegister(uint8_t reg) const;
    void writeRegister(uint8_t reg, uint8_t value) const;
    uint8_t keyCount() const;
    void configureMatrix() const;
    void flushEvents() const;
    char decodeKey(uint8_t matrixIndex, bool pressed);
    void maybeRepeatBackspace();
    static bool setBrightness(uint8_t pwm);
    void setSpareRowGate(bool lit) const;

    InputMode _mode = InputMode::Navigation;
    KeyEvent _event = {};
    bool _hasEvent = false;
    uint8_t _lastKey = 0;
    bool _altHeld = false;
    bool _capsOn = false;
    bool _backspaceHeld = false;        // TCA8418 FIFO reports press+release; tracked for auto-repeat
    uint32_t _backspaceNextRepeatMs = 0;
    uint8_t _backlightBrightness = 255; // [31, 255]
    bool _backlightLit = false;         // last commanded state; begin() starts dark

    static Keyboard* _instance;
    static int _debugCount;          // Log first N keypresses
};
