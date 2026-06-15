#include "Power.h"
#include "hal/Display.h"
#include "hal/Keyboard.h"
#include <Wire.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

// Forward declarations — display & keyboard instances provided externally
extern Display display;
extern Keyboard keyboard;

namespace {
constexpr uint8_t XL_REG_OUTPUT_0 = 0x02;
constexpr uint8_t XL_REG_OUTPUT_1 = 0x03;
constexpr uint8_t XL_REG_CONFIG_0 = 0x06;
constexpr uint8_t XL_REG_CONFIG_1 = 0x07;

bool xlWrite8(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(XL9555_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool xlRead8(uint8_t reg, uint8_t& value) {
    Wire.beginTransmission(XL9555_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) return false;
    if (Wire.requestFrom((uint8_t)XL9555_ADDR, (uint8_t)1) != 1) return false;
    value = Wire.read();
    return true;
}

bool xlWrite16(uint8_t regLo, uint16_t value) {
    return xlWrite8(regLo, value & 0xFF) && xlWrite8(regLo + 1, value >> 8);
}

bool xlRead16(uint8_t regLo, uint16_t& value) {
    uint8_t lo = 0, hi = 0;
    if (!xlRead8(regLo, lo) || !xlRead8(regLo + 1, hi)) return false;
    value = (uint16_t)lo | ((uint16_t)hi << 8);
    return true;
}

uint16_t xlBit(uint8_t pin) {
    return (uint16_t)1U << pin;
}

uint16_t peripheralRails() {
    return xlBit(XL9555_DRV_EN) |
           xlBit(XL9555_AMP_EN) |
           xlBit(XL9555_KB_RST) |
           xlBit(XL9555_LORA_EN) |
           xlBit(XL9555_GPS_EN) |
           xlBit(XL9555_NFC_EN) |
           xlBit(XL9555_GPS_RST) |
           xlBit(XL9555_KB_EN) |
           xlBit(XL9555_GPIO_EN) |
           xlBit(XL9555_SD_PULLEN) |
           xlBit(XL9555_SD_EN);
}

// BQ25896 charger PMIC
constexpr uint8_t BQ_REG_WATCHDOG = 0x07;  // [5:4] I2C watchdog, 00 = off
constexpr uint8_t BQ_REG_BATFET   = 0x09;  // [5] BATFET_DIS, [3] BATFET_DLY
constexpr uint8_t BQ_REG_STATUS   = 0x0B;  // [2] PG_STAT (VBUS power good)
constexpr uint8_t BQ_REG_PART     = 0x14;  // [5:3] part number

// BQ27220 fuel gauge
constexpr uint8_t GAUGE_CMD_VOLTAGE = 0x08;  // mV
constexpr uint8_t GAUGE_CMD_SOC     = 0x2C;  // %

bool bqRead8(uint8_t reg, uint8_t& value) {
    Wire.beginTransmission(BQ25896_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) return false;
    if (Wire.requestFrom((uint8_t)BQ25896_ADDR, (uint8_t)1) != 1) return false;
    value = Wire.read();
    return true;
}

bool bqWrite8(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(BQ25896_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool gaugeRead16(uint8_t cmd, uint16_t& value) {
    Wire.beginTransmission(BQ27220_ADDR);
    Wire.write(cmd);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)BQ27220_ADDR, (uint8_t)2) != 2) return false;
    uint16_t lo = Wire.read();
    uint16_t hi = Wire.read();
    value = lo | (hi << 8);
    return true;
}
}  // namespace

void Power::enablePeripherals() {
    uint16_t outputs = 0;
    uint16_t config = 0xFFFF;
    xlRead16(XL_REG_OUTPUT_0, outputs);
    xlRead16(XL_REG_CONFIG_0, config);

    const uint16_t rails = peripheralRails();

    outputs |= rails;
    config &= ~rails;                // 0 = output, 1 = input on XL9555/PCA9555
    config |= xlBit(XL9555_SD_DET);  // card detect remains input

    bool ok = xlWrite16(XL_REG_OUTPUT_0, outputs) && xlWrite16(XL_REG_CONFIG_0, config);
    Serial.printf("[POWER] XL9555 peripheral rails %s\n", ok ? "enabled" : "not detected");
    delay(20);
}

void Power::begin() {
    _lastActivity = millis();
    _state = ACTIVE;

    if (BAT_ADC_PIN >= 0) {
        pinMode(BAT_ADC_PIN, INPUT);
        analogReadResolution(12);
    }

    pinMode(BTN_BOOT, INPUT_PULLUP);

    // Probe the charger and re-arm the battery path: BATFET_DIS survives a
    // USB-powered "off" (the charger stays VBUS-fed), and a boot must never
    // run with it latched or the device dies the moment USB is unplugged.
    uint8_t reg9 = 0, reg0b = 0, part = 0;
    if (bqRead8(BQ_REG_BATFET, reg9) && bqRead8(BQ_REG_STATUS, reg0b) && bqRead8(BQ_REG_PART, part)) {
        Serial.printf("[POWER] BQ25896@0x%02X ok REG09=0x%02X REG0B=0x%02X REG14=0x%02X\n",
                      BQ25896_ADDR, reg9, reg0b, part);
        if (reg9 & 0x20) {
            bqWrite8(BQ_REG_BATFET, reg9 & ~0x20);
            Serial.println("[POWER] Cleared stale BATFET_DIS latch");
        }
    } else {
        Serial.printf("[POWER] charger not responding at 0x%02X — bus scan:", BQ25896_ADDR);
        for (uint8_t a = 0x08; a <= 0x77; ++a) {
            Wire.beginTransmission(a);
            if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", a);
        }
        Serial.println();
    }

    uint16_t gaugeMv = 0, gaugeSoc = 0;
    if (gaugeRead16(GAUGE_CMD_VOLTAGE, gaugeMv) && gaugeRead16(GAUGE_CMD_SOC, gaugeSoc)) {
        Serial.printf("[POWER] BQ27220@0x%02X ok batt=%umV soc=%u%%\n",
                      BQ27220_ADDR,
                      static_cast<unsigned int>(gaugeMv),
                      static_cast<unsigned int>(gaugeSoc > 100 ? 100 : gaugeSoc));
    } else {
        Serial.printf("[POWER] fuel gauge not responding at 0x%02X\n", BQ27220_ADDR);
    }

    Serial.println("[POWER] Power manager initialized");
}

float Power::batteryVoltage() const {
    uint16_t mv = 0;
    if (gaugeRead16(GAUGE_CMD_VOLTAGE, mv)) {
        return mv / 1000.0f;
    }

    if (BAT_ADC_PIN < 0) return -1.0f;
    int raw = analogRead(BAT_ADC_PIN);
    // Voltage divider: 2x ratio, 3.3V reference, 12-bit ADC
    return (raw / 4095.0f) * 3.3f * 2.0f;
}

int Power::batteryPercent() const {
    uint16_t soc = 0;
    if (gaugeRead16(GAUGE_CMD_SOC, soc)) {
        return soc > 100 ? 100 : soc;
    }

    float v = batteryVoltage();
    if (v < 0.0f) return -1;
    // LiPo voltage curve approximation
    if (v >= 4.2f) return 100;
    if (v <= 3.0f) return 0;
    return (int)((v - 3.0f) / 1.2f * 100.0f);
}

uint8_t Power::percentToPWM(uint8_t pct) const {
    if (pct == 0) return 0;
    if (pct >= 100) return 255;
    // Map 1-100 to ~6-255 (minimum visible PWM ~6)
    return (uint8_t)(6 + (uint16_t)(pct - 1) * 249 / 99);
}

void Power::activity() {
    _lastActivity = millis();
    if (_state == SCREEN_OFF) {
        _justWokeFromOff = true;
    }
    if (_state != ACTIVE) {
        setState(ACTIVE);
    }
}

void Power::forceScreenOff() {
    if (_justWokeFromOff) {
        _justWokeFromOff = false;
        return;
    }
    setState(SCREEN_OFF);
}

void Power::weakActivity() {
    _lastActivity = millis();
    // Encoder movement wakes from DIM but not from SCREEN_OFF
    if (_state == DIMMED) {
        setState(ACTIVE);
    }
}

void Power::setBrightness(uint8_t percent) {
    _brightnessPct = constrain(percent, 1, 100);
    if (_state == ACTIVE) {
        display.setBrightness(percentToPWM(_brightnessPct));
    }
}

void Power::setKbBrightness(uint8_t percent, bool apply) {
    percent = constrain(percent, 0, 100);
    keyboard.setBacklightBrightness(percent);
    if (percent == 0) {
        keyboard.backlightOff();
    } else if (apply) { // Show the new brightness
        keyboard.backlightOn();
    }
}

void Power::pollBootButton() {
    bool down = digitalRead(BTN_BOOT) == LOW;
    if (!down) {
        _btnWasDown = false;
        _btnFromScreenOn = false;
        _gestureLatched = false;
        return;
    }
    if (!_btnWasDown) {
        _btnWasDown = true;
        _btnFromScreenOn = isScreenOn();
        _btnDownMs = millis();
        if (_btnFromScreenOn) {
            _gestureLatched = true;
            _gesturePending = true;
        } else {
            activity();
        }
        return;
    }
    unsigned long held = millis() - _btnDownMs;
    if (held >= POWEROFF_FORCE_MS) {
        Serial.println("[POWER] BOOT force-hold");
        powerOff();
    } else if (_btnFromScreenOn && !_gestureLatched) {
        _gestureLatched = true;
        _gesturePending = true;
    }
}

bool Power::powerOffGestureFired() {
    bool fired = _gesturePending;
    _gesturePending = false;
    return fired;
}

bool Power::vbusPresent() const {
    uint8_t status = 0;
    return bqRead8(BQ_REG_STATUS, status) && (status & 0x04);
}

void Power::disablePeripherals() {
    uint16_t outputs = 0;
    if (xlRead16(XL_REG_OUTPUT_0, outputs)) {
        xlWrite16(XL_REG_OUTPUT_0, outputs & ~peripheralRails());
    }
}

void Power::powerOff() {
    Serial.println("[POWER] Power off");

    display.sleep();
    keyboard.backlightOff();

    // Wait out a still-held BOOT so the ext0 fallback below can't instantly
    // re-wake (bounded in case the pin is stuck).
    unsigned long waitStart = millis();
    while (digitalRead(BTN_BOOT) == LOW && millis() - waitStart < 15000) delay(10);
    delay(50);

    // Ship mode before touching the rails — the charger must get its I2C
    // writes while the bus is in its known-good powered state. Watchdog off
    // so the latch sticks, BATFET_DLY off, BATFET_DIS on. On battery, power
    // is gone milliseconds after the BATFET write; everything past it only
    // runs VBUS-fed or after a failed write.
    uint8_t reg = 0;
    bool ok = bqRead8(BQ_REG_WATCHDOG, reg) && bqWrite8(BQ_REG_WATCHDOG, reg & ~0x30);
    Serial.printf("[POWER] watchdog off: %s\n", ok ? "ok" : "FAIL");
    ok = bqRead8(BQ_REG_BATFET, reg) && bqWrite8(BQ_REG_BATFET, (reg & ~0x08) | 0x20);
    Serial.printf("[POWER] BATFET_DIS write: %s\n", ok ? "ok" : "FAIL");
    delay(100);
    if (bqRead8(BQ_REG_BATFET, reg)) {
        Serial.printf("[POWER] REG09 readback 0x%02X — BATFET_DIS %s\n",
                      reg, (reg & 0x20) ? "set" : "NOT set");
    }
    Serial.println("[POWER] still powered (VBUS or charger failure) — deep sleep park");
    Serial.flush();

    disablePeripherals();

    // Wake on encoder click. NOT BOOT/GPIO0: it's a strapping pin, and a
    // held BOOT at the wake edge straps the ROM into download mode — the
    // device looks dead until RST.
    rtc_gpio_pullup_en((gpio_num_t)ROTARY_CLICK);
    rtc_gpio_pulldown_dis((gpio_num_t)ROTARY_CLICK);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)ROTARY_CLICK, 0);
    esp_deep_sleep_start();
}

void Power::loop() {
    pollBootButton();

    unsigned long elapsed = millis() - _lastActivity;

    switch (_state) {
        case ACTIVE:
            if (_offTimeout > 0 && elapsed >= _offTimeout) {
                setState(SCREEN_OFF);
            } else if (_dimTimeout > 0 && elapsed >= _dimTimeout) {
                setState(DIMMED);
            }
            break;

        case DIMMED:
            if (_offTimeout > 0 && elapsed >= _offTimeout) {
                setState(SCREEN_OFF);
            }
            break;

        case SCREEN_OFF:
            break;
    }

    _justWokeFromOff = false;
}

void Power::setState(State newState) {
    if (newState == _state) return;
    const char* names[] = {"ACTIVE", "DIMMED", "SCREEN_OFF"};
    Serial.printf("[POWER] %s -> %s\n", names[_state], names[newState]);
    State oldState = _state;
    _state = newState;

    switch (_state) {
        case ACTIVE:
            if (oldState == SCREEN_OFF) {
                // Pre-load correct brightness into LovyanGFX state before wakeup.
                // wakeup() sends SLPOUT then restores LGFX's internal _brightness
                // to the LEDC — with this ordering, it restores the correct value
                // instead of a stale 0, eliminating the rapid 0→0→correct triple-
                // write that can cause missed LEDC duty updates on ESP32-S3.
                display.setBrightness(percentToPWM(_brightnessPct));
                display.wakeup();
            } else {
                display.setBrightness(percentToPWM(_brightnessPct));
            }
            // On wake, relight only what screen-off forced dark (or per auto-on) —
            // never force-enable for users who keep the kb light off.
            if (_kbAutoOn || (oldState == SCREEN_OFF && _kbLitBeforeOff)) {
                keyboard.backlightOn();
            }
            break;
        case DIMMED:
            display.setBrightness(DIM_PWM);
            if (_kbAutoOff) {
                keyboard.backlightOff();
            }
            break;
        case SCREEN_OFF:
            // LovyanGFX sleep() sets brightness to 0 internally — no
            // need to call setBrightness(0) beforehand.
            display.sleep();
            // Kb backlight always follows screen-off — the timeout exists to save battery.
            _kbLitBeforeOff = keyboard.backlightIsLit();
            keyboard.backlightOff();
            break;
    }
}
