#include "Keyboard.h"
#include <ctype.h>

Keyboard* Keyboard::_instance = nullptr;
int Keyboard::_debugCount = 0;

namespace {
constexpr uint8_t REG_CFG = 0x01;
constexpr uint8_t REG_INT_STAT = 0x02;
constexpr uint8_t REG_KEY_LCK_EC = 0x03;
constexpr uint8_t REG_KEY_EVENT_A = 0x04;
constexpr uint8_t REG_GPIO_DAT_OUT_1 = 0x19;
constexpr uint8_t REG_KP_GPIO_1 = 0x1D;
constexpr uint8_t REG_KP_GPIO_2 = 0x1E;
constexpr uint8_t REG_KP_GPIO_3 = 0x1F;
constexpr uint8_t REG_GPIO_DIR_1 = 0x23;
constexpr uint8_t REG_DEBOUNCE_DIS_1 = 0x29;
constexpr uint8_t REG_DEBOUNCE_DIS_2 = 0x2A;
constexpr uint8_t REG_DEBOUNCE_DIS_3 = 0x2B;
constexpr uint8_t CFG_KEY_EVENT_IRQ = 0x01;
constexpr uint8_t KEY_ROWS = 4;
constexpr uint8_t KEY_COLS = 10;
constexpr uint8_t KEY_ALT = 20;       // Row 2, col 0
constexpr uint8_t KEY_CAPS = 28;      // Row 2, col 8
constexpr uint8_t KEY_BACKSPACE = 29; // Row 2, col 9
constexpr uint8_t KEY_SPACE = 30;     // Row 3, col 0
constexpr uint8_t SPARE_ROWS = 0xF0;  // R4-R7, unused by the 4x10 matrix
// LilyGoLib's LEDC_BACKLIGHT_CHANNEL (timer 2). Channels 0/1 share timer 0,
// so channel 1 here let kb init reprogram the display backlight's timer
// (LGFX Light_PWM is channel 0, 9-bit/12 kHz) and vice versa in the launcher.
constexpr uint8_t KB_PWM_CHANNEL = 4;
constexpr uint32_t KB_PWM_FREQ = 1000;
constexpr uint8_t KB_PWM_BITS = 8;

// Backspace hold-to-repeat cadence: first synthesized repeat after
// INITIAL_DELAY_MS of hold, then one every REPEAT_MS (~12/sec).
constexpr uint32_t INITIAL_DELAY_MS = 400;
constexpr uint32_t REPEAT_MS = 85;

constexpr char KEYMAP[KEY_ROWS][KEY_COLS] = {
    {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'},
    {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', '\n'},
    {'\0', 'z', 'x', 'c', 'v', 'b', 'n', 'm', '\0', '\0'},
    {' ',  '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'}
};

constexpr char SYMBOL_MAP[KEY_ROWS][KEY_COLS] = {
    {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'},
    {'*', '/', '+', '-', '=', ':', '\'', '"', '@', '\0'},
    {'\0', '_', '$', ';', '?', '!', ',', '.', '\0', '\0'},
    {' ',  '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'}
};
}  // namespace

bool Keyboard::begin() {
    _instance = this;
    _mode = InputMode::Navigation;
    _hasEvent = false;

    // LilyGoLib order: park the pin low before the LEDC attach.
    pinMode(KB_BACKLIGHT, OUTPUT);
    digitalWrite(KB_BACKLIGHT, LOW);
    ledcSetup(KB_PWM_CHANNEL, KB_PWM_FREQ, KB_PWM_BITS);
    ledcAttachPin(KB_BACKLIGHT, KB_PWM_CHANNEL);

    pinMode(KB_INT, INPUT_PULLUP);

    Wire.beginTransmission(KB_I2C_ADDR);
    uint8_t err = Wire.endTransmission();
    if (err != 0) {
        Serial.printf("[KEYBOARD] TCA8418 not found at 0x%02X (err=%d)\n", KB_I2C_ADDR, err);
        return false;
    }

    configureMatrix();
    // Start dark on both control paths; boot applies the persisted brightness.
    backlightOff();
    flushEvents();
    writeRegister(REG_INT_STAT, 0x1F);
    writeRegister(REG_CFG, CFG_KEY_EVENT_IRQ);

    _altHeld = false;
    _capsOn = false;
    _backspaceHeld = false;
    _lastKey = 0;

    Serial.println("[KEYBOARD] TCA8418 keyboard ready");
    return true;
}

uint8_t Keyboard::readRegister(uint8_t reg) const {
    Wire.beginTransmission(KB_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) return 0;
    Wire.requestFrom((uint8_t)KB_I2C_ADDR, (uint8_t)1);
    if (!Wire.available()) return 0;
    return Wire.read();
}

void Keyboard::writeRegister(uint8_t reg, uint8_t value) const {
    Wire.beginTransmission(KB_I2C_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

uint8_t Keyboard::keyCount() const {
    return readRegister(REG_KEY_LCK_EC) & 0x0F;
}

void Keyboard::configureMatrix() const {
    writeRegister(REG_KP_GPIO_1, 0x0F); // 4 rows
    writeRegister(REG_KP_GPIO_2, 0xFF); // 8 columns
    writeRegister(REG_KP_GPIO_3, 0x03); // columns 8-9
    writeRegister(REG_DEBOUNCE_DIS_1, 0x00);
    writeRegister(REG_DEBOUNCE_DIS_2, 0x00);
    writeRegister(REG_DEBOUNCE_DIS_3, 0x00);
}

void Keyboard::flushEvents() const {
    while (readRegister(REG_KEY_EVENT_A) != 0) {}
    writeRegister(REG_INT_STAT, 0x1F);
}

char Keyboard::decodeKey(uint8_t matrixIndex, bool pressed) {
    if (matrixIndex == KEY_ALT) {
        _altHeld = pressed;
        return 0;
    }
    if (matrixIndex == KEY_CAPS) {
        if (pressed) _capsOn = !_capsOn;
        return 0;
    }
    if (matrixIndex == KEY_BACKSPACE) {
        // FIFO reports press and release; track real held state for auto-repeat.
        _backspaceHeld = pressed;
        if (pressed) _backspaceNextRepeatMs = millis() + INITIAL_DELAY_MS;
        return pressed ? '\b' : 0;
    }
    if (!pressed) return 0;
    if (matrixIndex == KEY_SPACE) return ' ';

    uint8_t row = matrixIndex / KEY_COLS;
    uint8_t col = matrixIndex % KEY_COLS;
    if (row >= KEY_ROWS || col >= KEY_COLS) return 0;

    char key = _altHeld ? SYMBOL_MAP[row][col] : KEYMAP[row][col];
    if (!_altHeld && _capsOn && key >= 'a' && key <= 'z') {
        key = (char)toupper(key);
    }
    return key;
}

// Synthesize repeated backspace events while the key is held. Marked with
// repeat=true so back/dismiss handlers can ignore them (only text deletion
// consumes repeats; a held backspace empties a draft but never navigates).
void Keyboard::maybeRepeatBackspace() {
    if (!_backspaceHeld) return;
    uint32_t now = millis();
    if ((int32_t)(now - _backspaceNextRepeatMs) < 0) return;
    _backspaceNextRepeatMs = now + REPEAT_MS;
    _event = {};
    _event.del = true;
    _event.character = 0x08;
    _event.repeat = true;
    _event.alt = _altHeld;
    _event.shift = _capsOn;
    _hasEvent = true;
}

void Keyboard::update() {
    _hasEvent = false;
    uint8_t count = keyCount();
    if (count == 0) {
        maybeRepeatBackspace();
        return;
    }

    _event = {};
    char key = 0;
    uint8_t raw = 0;
    bool rawPressed = false;

    for (uint8_t i = 0; i < count; ++i) {
        raw = readRegister(REG_KEY_EVENT_A);
        if (raw == 0) continue;
        rawPressed = (raw & 0x80) != 0;
        uint8_t keyCode = raw & 0x7F;
        if (keyCode == 0) continue;
        char decoded = decodeKey(keyCode - 1, rawPressed);
        if (decoded != 0 && key == 0) key = decoded;
    }
    writeRegister(REG_INT_STAT, 0x1F);

    if (key == 0) {
        // FIFO held only releases/modifiers (incl. backspace-up, which clears held state)
        maybeRepeatBackspace();
        return;
    }

    if (_debugCount < 50) {
        _debugCount++;
        Serial.printf("[KB] raw=0x%02X %s key=0x%02X ('%c') alt=%d caps=%d\n",
                      raw, rawPressed ? "down" : "up", (uint8_t)key,
                      (key >= 0x20 && key < 0x7F) ? key : '?',
                      _altHeld, _capsOn);
    }

    _event.alt = _altHeld;
    _event.shift = _capsOn;

    // Standard key decoding
    if (key == 0x0D || key == '\n') {
        _event.enter = true;
        _event.character = '\n';
    } else if (key == 0x08 || key == 0x7F) {
        _event.del = true;
        _event.character = 0x08;
    } else if (key == 0x09) {
        _event.tab = true;
    } else if (key == 0x1B) {
        _event.character = 27;  // ESC
    } else if (key == ' ') {
        _event.space = true;
        _event.character = ' ';
    } else if (key >= 0x20 && key <= 0x7E) {
        _event.character = key;
    }

    _hasEvent = true;
}

bool Keyboard::setBacklightBrightness(uint8_t percent) {
    percent = constrain(percent, 0, 100);
    if (percent == 0) {
        _backlightBrightness = 0;
        return true;
    }
    // [1, 100] % -> [31, 255] PWM
    constexpr uint16_t SCALE = 255 - 31;
    constexpr uint16_t DIV   = 100 - 1;
    uint16_t tmp = (uint16_t)(percent - 1) * SCALE;
    tmp = (tmp + DIV / 2) / DIV; // +DIV/2 for nearest‑integer rounding
    _backlightBrightness = (uint8_t)(31 + tmp);
    // PWM is host-driven via LEDC; applied on next backlightOn(). (The old I2C
    // brightness command here was a T-Deck C3 leftover — 0x34 is the TCA8418.)
    return true;
}

bool Keyboard::backlightOn() {
    _backlightLit = _backlightBrightness > 0;
    setSpareRowGate(_backlightLit);
    return setBrightness(_backlightBrightness);
}

bool Keyboard::backlightOff() {
    _backlightLit = false;
    setSpareRowGate(false);
    return setBrightness(0);
}

bool Keyboard::setBrightness(uint8_t pwm) {
    ledcWrite(KB_PWM_CHANNEL, pwm);
    return true;
}

// LilyGoLib + the V1.0 schematic drive the kb LEDs with LEDC PWM on GPIO46
// (LED_EN -> 1k -> S8050 -> LED cathode rail, anodes on KEY_VDD). Keyboard
// PCBs whose LED gate instead hangs off a spare TCA8418 row power up lit:
// the chip resets to GPIO-input + pull-up on every pin, which biases the
// gate on from the moment KEY_VDD rises. Mirror on/off there: off = drive
// R4-R7 low, on = restore the chip-default input + pull-up. Never drives
// high, so NC or grounded spares see no contention. Dimming only exists on
// the PWM path; this gate is binary.
void Keyboard::setSpareRowGate(bool lit) const {
    uint8_t dir = readRegister(REG_GPIO_DIR_1);
    if (lit) {
        writeRegister(REG_GPIO_DIR_1, dir & ~SPARE_ROWS);
    } else {
        uint8_t dat = readRegister(REG_GPIO_DAT_OUT_1);
        writeRegister(REG_GPIO_DAT_OUT_1, dat & ~SPARE_ROWS);
        writeRegister(REG_GPIO_DIR_1, dir | SPARE_ROWS);
    }
}
