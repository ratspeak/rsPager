// rsPager boot launcher — picks Standalone or RNode at power-on.
// Runs from ota_0; writes the choice to otadata and restarts. Both target
// firmwares re-arm this launcher on boot, so any reset returns here.
// Input reuses the standalone HAL (Scrollwheel encoder, TCA8418 Keyboard) via
// shim includes. T-Pager has no touch panel.

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <LovyanGFX.hpp>

#include "RsPagerModeSwitch.h"
#include "config/BoardConfig.h"
#include "hal/Scrollwheel.h"
#include "hal/Keyboard.h"

namespace {

constexpr uint16_t kBg = 0x0841;
constexpr uint16_t kPanel = 0x1082;
constexpr uint16_t kText = 0xF7BE;
constexpr uint16_t kMuted = 0x8C71;
constexpr uint16_t kAccent = 0x06D7;
constexpr uint16_t kWarn = 0xFBA0;
constexpr uint32_t kAutoBootMs = 7000;
constexpr char kPrefsNamespace[] = "rslaunch";
constexpr char kLastChoiceKey[] = "last";

// Option card geometry (480x222 landscape)
constexpr int16_t kCardX = 24;
constexpr int16_t kCardW = 432;
constexpr int16_t kCardH = 52;
constexpr int16_t kCard1Y = 58;
constexpr int16_t kCard2Y = 116;

// Same panel wiring as the standalone firmware (src/hal/Display.h):
// ST7796U wired as 222x480 portrait, used as 480x222 landscape via rotation 3.
class LGFX_TPagerLauncher : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;

public:
  LGFX_TPagerLauncher() {
    auto cfg_bus = _bus.config();
    cfg_bus.spi_host = SPI2_HOST;
    cfg_bus.spi_mode = 0;
    cfg_bus.freq_write = TFT_SPI_FREQ;
    cfg_bus.freq_read = 16000000;
    cfg_bus.pin_sclk = SPI_SCK;
    cfg_bus.pin_miso = SPI_MISO;
    cfg_bus.pin_mosi = SPI_MOSI;
    cfg_bus.pin_dc = TFT_DC;
    _bus.config(cfg_bus);
    _panel.setBus(&_bus);

    auto cfg_panel = _panel.config();
    cfg_panel.pin_cs = TFT_CS;
    cfg_panel.pin_rst = TFT_RST;
    cfg_panel.panel_width = TFT_NATIVE_WIDTH;
    cfg_panel.panel_height = TFT_NATIVE_HEIGHT;
    cfg_panel.offset_x = 49;
    cfg_panel.offset_y = 0;
    cfg_panel.invert = true;
    cfg_panel.rgb_order = false;
    cfg_panel.memory_width = 320;
    cfg_panel.memory_height = TFT_NATIVE_HEIGHT;
    _panel.config(cfg_panel);

    auto cfg_light = _light.config();
    cfg_light.pin_bl = TFT_BL;
    cfg_light.invert = false;
    cfg_light.freq = 12000;
    cfg_light.pwm_channel = 0;
    _light.config(cfg_light);
    _panel.setLight(&_light);

    setPanel(&_panel);
  }
};

LGFX_TPagerLauncher display;
Scrollwheel scrollwheel;
Keyboard keyboard;

// XL9555 expander register map (mirrors src/hal/Power.cpp).
constexpr uint8_t XL_REG_OUTPUT_0 = 0x02;
constexpr uint8_t XL_REG_CONFIG_0 = 0x06;

bool xlWrite8(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(XL9555_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool xlRead8(uint8_t reg, uint8_t &value) {
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

bool xlRead16(uint8_t regLo, uint16_t &value) {
  uint8_t lo = 0, hi = 0;
  if (!xlRead8(regLo, lo) || !xlRead8(regLo + 1, hi)) return false;
  value = (uint16_t)lo | ((uint16_t)hi << 8);
  return true;
}

uint16_t xlBit(uint8_t pin) {
  return (uint16_t)1U << pin;
}

// T-Pager rails live behind the XL9555 I2C expander, not a GPIO. Same rail
// bring-up as the standalone firmware's Power::enablePeripherals().
void enablePeripheralRails() {
  uint16_t outputs = 0;
  uint16_t config = 0xFFFF;
  xlRead16(XL_REG_OUTPUT_0, outputs);
  xlRead16(XL_REG_CONFIG_0, config);

  const uint16_t rails =
      xlBit(XL9555_DRV_EN) |
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

  outputs |= rails;
  config &= ~rails;                // 0 = output, 1 = input on XL9555/PCA9555
  config |= xlBit(XL9555_SD_DET);  // card detect remains input

  bool ok = xlWrite16(XL_REG_OUTPUT_0, outputs) && xlWrite16(XL_REG_CONFIG_0, config);
  Serial.printf("[LAUNCHER] XL9555 peripheral rails %s\n", ok ? "enabled" : "not detected");
  delay(20);
}

enum class Choice : uint8_t {
  Standalone = 0,
  RNode = 1,
};

Choice selected = Choice::Standalone;
uint32_t bootStarted = 0;
uint32_t lastRemain = UINT32_MAX;
bool booting = false;
bool autoBootEnabled = true;

int16_t scrollAccum = 0;
uint32_t lastNavMove = 0;

uint8_t choiceValue(Choice choice) {
  return choice == Choice::RNode ? 1 : 0;
}

Choice choiceFromValue(uint8_t value) {
  return value == 1 ? Choice::RNode : Choice::Standalone;
}

Choice loadLastChoice() {
  Preferences prefs;
  Choice choice = Choice::Standalone;
  if (prefs.begin(kPrefsNamespace, true)) {
    choice = choiceFromValue(prefs.getUChar(kLastChoiceKey, choiceValue(choice)));
    prefs.end();
  }
  return choice;
}

void saveLastChoice(Choice choice) {
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.putUChar(kLastChoiceKey, choiceValue(choice));
    prefs.end();
  }
}

void drawOption(int16_t y, const char *title, const char *subtitle, bool active) {
  uint16_t fill = active ? kAccent : kPanel;
  uint16_t fg = active ? TFT_BLACK : kText;
  uint16_t sub = active ? 0x2104 : kMuted;

  display.fillRoundRect(kCardX, y, kCardW, kCardH, 6, fill);
  display.setTextColor(fg, fill);
  display.setTextSize(2);
  display.setCursor(kCardX + 16, y + 9);
  display.print(title);
  display.setTextColor(sub, fill);
  display.setTextSize(1);
  display.setCursor(kCardX + 16, y + 32);
  display.print(subtitle);
}

uint32_t remainingSeconds() {
  uint32_t elapsed = millis() - bootStarted;
  if (elapsed >= kAutoBootMs) {
    return 0;
  }
  return (kAutoBootMs - elapsed + 999) / 1000;
}

void drawCountdown(bool force = false) {
  if (!autoBootEnabled) {
    display.fillRect(440, 8, 34, 24, kBg);
    lastRemain = UINT32_MAX;
    return;
  }

  uint32_t remain = remainingSeconds();
  if (!force && remain == lastRemain) {
    return;
  }
  lastRemain = remain;

  display.fillRect(440, 8, 34, 24, kBg);
  display.fillRoundRect(446, 9, 26, 22, 6, kPanel);
  display.setTextSize(2);
  display.setTextColor(kText, kPanel);
  display.setCursor(remain >= 10 ? 449 : 454, 13);
  display.print(static_cast<unsigned long>(remain));
}

void drawScreen() {
  display.fillScreen(kBg);

  display.setTextSize(3);
  display.setTextColor(kText, kBg);
  display.setCursor(24, 12);
  display.print("rsPager");

  display.setTextSize(1);
  display.setTextColor(kMuted, kBg);
  display.setCursor(26, 42);
  display.print("T-Pager");

  drawOption(kCard1Y, "Standalone", "On-device messenger", selected == Choice::Standalone);
  drawOption(kCard2Y, "RNode", "BLE / USB radio", selected == Choice::RNode);

  display.setTextSize(1);
  display.setTextColor(kMuted, kBg);
  display.setCursor(24, 180);
  display.print("Wheel or W/S select, click/Enter boot");
  display.setCursor(24, 196);
  display.print("Q = Standalone now, A = RNode now");

  drawCountdown(true);
}

void selectChoice(Choice choice) {
  if (selected == choice) {
    return;
  }
  selected = choice;
  drawScreen();
}

void pauseAutoBoot() {
  if (!autoBootEnabled) {
    return;
  }
  autoBootEnabled = false;
  drawCountdown(true);
}

void showBooting(const char *label) {
  booting = true;
  display.fillScreen(kBg);
  display.setTextSize(3);
  display.setTextColor(kAccent, kBg);
  display.setCursor(28, 70);
  display.print(label);
  display.setTextSize(1);
  display.setTextColor(kMuted, kBg);
  display.setCursor(30, 110);
  display.print("Starting...");
}

void showError(const char *message) {
  booting = false;
  display.fillScreen(kBg);
  display.setTextSize(2);
  display.setTextColor(kWarn, kBg);
  display.setCursor(24, 60);
  display.print("Boot error");
  display.setTextSize(1);
  display.setTextColor(kText, kBg);
  display.setCursor(24, 96);
  display.print(message);
}

void startChoice(Choice choice) {
  using namespace rs_pager;

  FirmwareMode mode = choice == Choice::Standalone ? FirmwareMode::Standalone : FirmwareMode::RNode;
  Serial.printf("[LAUNCHER] booting %s\n", mode_name(mode));
  showBooting(mode_name(mode));
  SwitchResult result = set_next_boot(mode);
  if (!result.ok) {
    Serial.printf("[LAUNCHER] boot switch failed: %s\n", result.message);
    showError(result.message);
    return;
  }
  saveLastChoice(choice);
  delay(mode == FirmwareMode::RNode ? 1500 : 50);
  esp_restart();
}

void handleKey(char key) {
  pauseAutoBoot();

  if (key == '\r' || key == '\n') {
    startChoice(selected);
    return;
  }
  if (key == 'w' || key == 'W') {
    selectChoice(Choice::Standalone);
    return;
  }
  if (key == 's' || key == 'S') {
    selectChoice(Choice::RNode);
    return;
  }
  if (key == 'q' || key == 'Q') {
    startChoice(Choice::Standalone);
    return;
  }
  if (key == 'a' || key == 'A') {
    startChoice(Choice::RNode);
    return;
  }
}

void pollKeyboard() {
  keyboard.update();
  if (!keyboard.hasEvent()) {
    return;
  }
  const KeyEvent &event = keyboard.getEvent();
  if (event.enter) {
    handleKey('\r');
    return;
  }
  if (event.character != 0) {
    handleKey(event.character);
  }
}

void pollEncoder() {
  scrollwheel.update();

  if (scrollwheel.wasClicked()) {
    pauseAutoBoot();
    startChoice(selected);
    return;
  }

  int8_t dy = scrollwheel.lastDeltaY();
  if (dy != 0) {
    pauseAutoBoot();
    scrollAccum += dy;
    // Deltas are whole detents (ISR counts full quadrature cycles): one
    // detent per selection step, rate-limited so a flick moves once.
    uint32_t now = millis();
    if (now - lastNavMove > 150) {
      if (scrollAccum <= -1) {
        lastNavMove = now;
        scrollAccum = 0;
        selectChoice(Choice::Standalone);
      } else if (scrollAccum >= 1) {
        lastNavMove = now;
        scrollAccum = 0;
        selectChoice(Choice::RNode);
      }
    }
  }
}

} // namespace

void setup() {
  Serial.begin(115200);

  // Rails sit behind the XL9555 expander, so I2C must come up first.
  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  Wire.setTimeOut(20);
  enablePeripheralRails();
  delay(50);

  // Park the other chip selects on the shared SPI bus.
  pinMode(LORA_CS, OUTPUT);
  digitalWrite(LORA_CS, HIGH);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  scrollwheel.begin();
  keyboard.begin();

  display.init();
  display.setRotation(3);
  display.setBrightness(200);

  selected = loadLastChoice();
  bootStarted = millis();
  Serial.printf("[LAUNCHER] rsPager launcher up, last choice: %s\n",
                selected == Choice::RNode ? "RNode" : "Standalone");
  drawScreen();
}

void loop() {
  if (booting) {
    delay(20);
    return;
  }

  pollKeyboard();
  pollEncoder();
  drawCountdown();

  if (autoBootEnabled && millis() - bootStarted >= kAutoBootMs) {
    startChoice(selected);
    return;
  }

  delay(5);
}
