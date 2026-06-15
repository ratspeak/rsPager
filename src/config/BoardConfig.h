#pragma once

// =============================================================================
// rsPager - LilyGo T-Pager / T-LoRa-Pager Pin Definitions
// =============================================================================

// --- Power / IO Expander (XL9555, I2C 0x20) ---
#define XL9555_ADDR          0x20
#define XL9555_DRV_EN        0
#define XL9555_AMP_EN        1
#define XL9555_KB_RST        2
#define XL9555_LORA_EN       3
#define XL9555_GPS_EN        4
#define XL9555_NFC_EN        5
#define XL9555_GPS_RST       7
#define XL9555_KB_EN         8
#define XL9555_GPIO_EN       9
#define XL9555_SD_DET       10
#define XL9555_SD_PULLEN    11
#define XL9555_SD_EN        12

// --- SX1262 LoRa Radio (shared SPI bus) ---
#define LORA_CS             36
#define LORA_IRQ            14   // DIO1
#define LORA_RST            47
#define LORA_BUSY           48
#define LORA_RXEN           -1   // Not connected
#define LORA_TXEN           -1   // Not connected

// --- SX1262 Radio Configuration ---
#define LORA_HAS_TCXO           true
#define LORA_DIO2_AS_RF_SWITCH  true
#define LORA_TCXO_VOLTAGE       0x06     // MODE_TCXO_3_0V_6X
#define LORA_USE_DCDC_REGULATOR true
#define LORA_OCP_TUNED          0x38
#define LORA_DEFAULT_FREQ       915000000
#define LORA_DEFAULT_BW         250000   // Long Fast preset
#define LORA_DEFAULT_SF         11
#define LORA_DEFAULT_CR         5
#define LORA_DEFAULT_TX_POWER   22       // Long Fast preset
#define LORA_DEFAULT_PREAMBLE   18

// --- Shared SPI Bus (display + LoRa + SD) ---
#define SPI_SCK             35
#define SPI_MISO            33
#define SPI_MOSI            34

// --- Display (ST7796U via LovyanGFX) ---
#define TFT_CS              38
#define TFT_DC              37
#define TFT_BL              42   // Backlight PWM
#define TFT_RST             -1
#define TFT_WIDTH           480
#define TFT_HEIGHT          222
#define TFT_NATIVE_WIDTH    222
#define TFT_NATIVE_HEIGHT   480
#define TFT_SPI_FREQ        27000000

// --- Keyboard (TCA8418 matrix, 4x10) ---
#define KB_I2C_ADDR         0x34
#define KB_INT               6
#define KB_BACKLIGHT        46

// --- I2C Bus (keyboard + PMIC/gauge/RTC/IMU/audio/haptics/expander) ---
#define I2C_SDA              3
#define I2C_SCL              2

// --- Touchscreen ---
// T-Pager has no touch panel. LVGL receives keypad/encoder events only.
#define TOUCH_INT           -1
#define TOUCH_I2C_ADDR_1    0x00
#define TOUCH_I2C_ADDR_2    0x00

// --- Rotary encoder / scroll wheel ---
// Up/down quadrature plus center click. There is no left/right axis.
#define ROTARY_A            40
#define ROTARY_B            41
#define ROTARY_CLICK         7

// --- Side buttons (RST / BOOT / PWR, bottom-left edge) ---
// RST is the EN line (hardware reset, not readable). PWR is wired only to the
// BQ25896 QON pin: ~1s hold powers on from ship mode, ~12s hold triggers the
// silicon BATFET full-system reset. BOOT is the one host-readable button.
#define BTN_BOOT             0

// --- SD Card (shared SPI bus) ---
#define SD_CS               21

// --- GPS UART ---
#define GPS_TX              12   // ESP TX -> GPS RX
#define GPS_RX               4   // GPS TX -> ESP RX
#define GPS_BAUD            38400   // UBlox MIA-M10Q factory default

// --- Battery ---
#define BAT_ADC_PIN         -1
#define BQ27220_ADDR        0x55
#define BQ25896_ADDR        0x6B   // Charger PMIC — ship mode = true power off

// --- Audio (ES8311 codec path) ---
#define I2S_WS              18   // LRCK
#define I2S_DOUT            45
#define I2S_BCK             11
#define I2S_DIN             17
#define I2S_MCLK            10

// --- Hardware Constants ---
#define MAX_PACKET_SIZE     255
#define SPI_FREQUENCY       8000000   // 8 MHz SPI clock for SX1262
