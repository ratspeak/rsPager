#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "config/BoardConfig.h"

// LovyanGFX display configuration for T-Pager
// Hardware panel is ST7796U wired as 222x480 portrait, used in 480x222 landscape.
class LGFX_TPager : public lgfx::LGFX_Device {
    lgfx::Panel_ST7796 _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Light_PWM _light;

public:
    LGFX_TPager() {
        // SPI bus config
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

        // Panel config — native orientation is 222x480 portrait.
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

        // Backlight config
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

class Display {
public:
    bool begin();

    // Initialize LVGL display driver with double-buffered DMA flush
    bool beginLVGL();

    // Backlight
    void setBrightness(uint8_t level);
    void sleep();
    void wakeup();

    LGFX_TPager& gfx() { return _gfx; }

private:
    LGFX_TPager _gfx;
};
