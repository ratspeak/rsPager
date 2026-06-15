// Full-screen RNode UI for the LilyGo T-Pager (rsPager dual-boot build).
// Renders into a PSRAM-backed 480x222 canvas pushed to the ST7796 via
// LovyanGFX, replacing the generic 64x128 OLED layout. Included by Display.h
// for BOARD_TPAGER only. There is no touch panel: the encoder click (standard
// HAS_INPUT button path) wakes the display and arms BLE pairing on a long
// hold; any TCA8418 keyboard event also wakes the display.

#ifndef TPAGER_UI_H
#define TPAGER_UI_H

#include <Wire.h>

void display_unblank();
extern uint32_t bt_pairing_started;
#define TP_PAIRING_WINDOW_MS 30000

// Ratspeak-style dark palette (RGB565)
#define TP_CLR_BG            0x0041
#define TP_CLR_BG_PANEL      0x1082
#define TP_CLR_BG_STATUSBAR  0x0861
#define TP_CLR_BORDER        0x2945
#define TP_CLR_TEXT_PRIMARY  0xE73C
#define TP_CLR_TEXT_SECONDARY 0xAD75
#define TP_CLR_TEXT_DIM      0x630C
#define TP_CLR_ACCENT        0x4EBF
#define TP_CLR_GREEN         0x070D
#define TP_CLR_WARN          0xFD20
#define TP_CLR_ERROR         0xF8A2

#define TP_W 480
#define TP_H 222
#define TP_BAR_H 24
#define TP_FOOT_Y 208
#define TP_CONTENT_Y 30
#define TP_LEFT_W 300
#define TP_WF_X 306
#define TP_WF_W 170
#define TP_WF_Y 30
#define TP_WF_H 174
#define TP_WF_SIZE TP_WF_H

static const uint16_t tp_wf_palette[9] = {
  0x0000, 0x0011, 0x001F, 0x07FF,
  0x07E0, 0xFFE0, 0xFBE0, 0xF800, 0xFFFF
};

GFXcanvas16* tp_canvas = nullptr;
bool tp_ui_ready = false;
int tp_waterfall[TP_WF_SIZE];
int tp_waterfall_head = 0;
uint8_t tp_charge_tick = 0;

// --- TCA8418 keyboard (I2C 0x34, poll-only: any key event wakes the display).
// Register access mirrors the rsPager standalone HAL (src/hal/Keyboard.cpp).
#define TP_KB_ADDR           0x34
#define TP_KB_REG_INT_STAT   0x02
#define TP_KB_REG_KEY_LCK_EC 0x03
#define TP_KB_REG_KEY_EVENT_A 0x04
#define TP_KB_REG_KP_GPIO_1  0x1D
#define TP_KB_REG_KP_GPIO_2  0x1E
#define TP_KB_REG_KP_GPIO_3  0x1F
bool tp_kb_present = false;
uint32_t tp_last_kb_poll = 0;

uint8_t tp_kb_read(uint8_t reg) {
  Wire.beginTransmission(TP_KB_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return 0;
  if (Wire.requestFrom((uint8_t)TP_KB_ADDR, (uint8_t)1) != 1) return 0;
  return Wire.read();
}

void tp_kb_write(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(TP_KB_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

void tp_kb_init() {
  Wire.beginTransmission(TP_KB_ADDR);
  if (Wire.endTransmission() != 0) return;
  // 4x10 matrix; key events queue into the TCA8418 FIFO.
  tp_kb_write(TP_KB_REG_KP_GPIO_1, 0x0F);
  tp_kb_write(TP_KB_REG_KP_GPIO_2, 0xFF);
  tp_kb_write(TP_KB_REG_KP_GPIO_3, 0x03);
  while (tp_kb_read(TP_KB_REG_KEY_EVENT_A) != 0) { }
  tp_kb_write(TP_KB_REG_INT_STAT, 0x1F);
  tp_kb_present = true;
}

void tp_poll_keyboard() {
  if (!tp_kb_present) return;
  if (millis() - tp_last_kb_poll < 50) return;
  tp_last_kb_poll = millis();

  uint8_t count = tp_kb_read(TP_KB_REG_KEY_LCK_EC) & 0x0F;
  if (count == 0) return;
  while (tp_kb_read(TP_KB_REG_KEY_EVENT_A) != 0) { }
  tp_kb_write(TP_KB_REG_INT_STAT, 0x1F);

  display_unblank();
  last_disp_update = 0;
}

void tp_ui_init() {
  if (tp_ui_ready) return;
  display.setRotation(3);
  display.setSwapBytes(true); // canvas holds native-order RGB565
  display.fillScreen(TP_CLR_BG);
  tp_canvas = new GFXcanvas16(TP_W, TP_H);
  for (int i = 0; i < TP_WF_SIZE; i++) tp_waterfall[i] = 0;
  tp_kb_init();
  display_blanking_enabled = true;
  tp_ui_ready = true;
}

void tp_push_frame() {
  if (tp_canvas && tp_canvas->getBuffer()) {
    display.pushImage(0, 0, TP_W, TP_H, tp_canvas->getBuffer());
  }
}

uint16_t tp_load_color(float percent) {
  if (percent < 33.0f) return TP_CLR_GREEN;
  if (percent < 66.0f) return 0xFBE0;
  return 0xF800;
}

void tp_draw_gradient_bar(int x, int y, int w, int h, float percent) {
  if (percent > 100.0f) percent = 100.0f;
  if (percent < 0.0f) percent = 0.0f;
  int fill_w = (int)((percent / 100.0f) * (w - 2));
  uint16_t fill_clr = TP_CLR_GREEN;
  if (percent < 33.0f) fill_clr = 0xF800;
  else if (percent < 66.0f) fill_clr = 0xFBE0;
  tp_canvas->drawRect(x, y, w, h, TP_CLR_BORDER);
  if (fill_w > 0) tp_canvas->fillRect(x + 1, y + 1, fill_w, h - 2, fill_clr);
}

void tp_draw_usb_icon(int x, int y, uint16_t colour) {
  tp_canvas->fillRect(x + 3, y, 7, 10, colour);
  tp_canvas->fillRect(x + 4, y + 10, 5, 4, colour);
  tp_canvas->drawPixel(x + 1, y + 3, colour);
  tp_canvas->drawPixel(x + 11, y + 3, colour);
}

void tp_draw_bt_icon(int x, int y, uint16_t colour) {
  tp_canvas->drawLine(x + 5, y, x + 5, y + 13, colour);
  tp_canvas->drawLine(x + 5, y, x + 9, y + 4, colour);
  tp_canvas->drawLine(x + 9, y + 4, x + 0, y + 10, colour);
  tp_canvas->drawLine(x + 0, y + 3, x + 9, y + 9, colour);
  tp_canvas->drawLine(x + 9, y + 9, x + 5, y + 13, colour);
}

void tp_draw_lora_icon(int x, int y, uint16_t colour) {
  tp_canvas->fillCircle(x + 5, y + 7, 2, colour);
  tp_canvas->drawCircle(x + 5, y + 7, 5, colour);
  tp_canvas->drawCircle(x + 5, y + 7, 8, colour);
}

void tp_draw_battery(int x, int y) {
  if (!pmu_ready || !battery_ready || !battery_installed) {
    tp_canvas->setTextSize(1);
    tp_canvas->setTextColor(TP_CLR_TEXT_DIM);
    tp_canvas->setCursor(x, y + 1);
    tp_canvas->print("PWR");
    return;
  }

  float bval = battery_percent;
  bool known = !battery_indeterminate;
  bool charging = known && (battery_state == BATTERY_STATE_CHARGING);
  bool charged = known && (battery_state == BATTERY_STATE_CHARGED);
  uint16_t fill_clr = TP_CLR_GREEN;

  if (charged) { fill_clr = TP_CLR_ACCENT; bval = 100.0f; }
  else if (charging) {
    fill_clr = TP_CLR_ACCENT;
    tp_charge_tick += 3;
    if (tp_charge_tick > 100) tp_charge_tick = 0;
    bval = tp_charge_tick;
  } else if (bval <= 20.0f) fill_clr = 0xF800;
  else if (bval <= 60.0f) fill_clr = 0xFBE0;

  int bw = 22; int bh = 10;
  tp_canvas->drawRect(x, y, bw, bh, TP_CLR_TEXT_SECONDARY);
  tp_canvas->fillRect(x + bw, y + 3, 2, 4, TP_CLR_TEXT_SECONDARY);
  int fill_w = (int)((bval / 100.0f) * (bw - 2));
  if (fill_w > 0) tp_canvas->fillRect(x + 1, y + 1, fill_w, bh - 2, fill_clr);

  char bbuf[6];
  float pct = battery_percent;
  if (pct > 100.0f) pct = 100.0f;
  if (pct < 0.0f) pct = 0.0f;
  snprintf(bbuf, sizeof(bbuf), "%d%%", (int)(pct + 0.5f));
  tp_canvas->setTextSize(1);
  tp_canvas->setTextColor(TP_CLR_TEXT_SECONDARY);
  tp_canvas->setCursor(x + 28, y + 1);
  tp_canvas->print(bbuf);
}

void tp_draw_statusbar() {
  tp_canvas->fillRect(0, 0, TP_W, TP_BAR_H, TP_CLR_BG_STATUSBAR);

  tp_draw_usb_icon(6, 5, (cable_state == CABLE_STATE_CONNECTED) ? TP_CLR_ACCENT : TP_CLR_TEXT_DIM);

  uint16_t bt_clr = TP_CLR_TEXT_DIM;
  if (bt_state == BT_STATE_ON) bt_clr = TP_CLR_ACCENT;
  else if (bt_state == BT_STATE_PAIRING) bt_clr = TP_CLR_WARN;
  else if (bt_state == BT_STATE_CONNECTED) bt_clr = TP_CLR_GREEN;
  tp_draw_bt_icon(26, 5, bt_clr);

  tp_draw_lora_icon(46, 5, radio_online ? TP_CLR_GREEN : TP_CLR_TEXT_DIM);

  tp_canvas->setTextSize(2);
  tp_canvas->setTextColor(TP_CLR_TEXT_PRIMARY);
  tp_canvas->setCursor(192, 5);
  tp_canvas->print("Ratspeak");

  tp_draw_battery(420, 7);
  tp_canvas->drawFastHLine(0, TP_BAR_H, TP_W, TP_CLR_BORDER);
}

void tp_draw_pair_panel() {
  int cx = TP_WF_X + (TP_WF_W / 2);

  if (bt_state == BT_STATE_PAIRING) {
    uint32_t elapsed = millis() - bt_pairing_started;
    uint32_t remain_s = 0;
    if (elapsed < TP_PAIRING_WINDOW_MS) {
      remain_s = (TP_PAIRING_WINDOW_MS - elapsed + 999) / 1000;
    }
    tp_canvas->setTextSize(1);
    tp_canvas->setTextColor(TP_CLR_WARN);
    tp_canvas->setCursor(cx - 21, TP_WF_Y + 44);
    tp_canvas->print("PAIRING");
    char rbuf[4];
    snprintf(rbuf, sizeof(rbuf), "%lu", (unsigned long)remain_s);
    tp_canvas->setTextSize(4);
    tp_canvas->setTextColor(TP_CLR_TEXT_PRIMARY);
    tp_canvas->setCursor(remain_s >= 10 ? cx - 23 : cx - 11, TP_WF_Y + 64);
    tp_canvas->print(rbuf);
    tp_canvas->setTextSize(1);
    tp_canvas->setTextColor(TP_CLR_TEXT_DIM);
    tp_canvas->setCursor(cx - 45, TP_WF_Y + 106);
    tp_canvas->print("click to cancel");
    return;
  }

  if (bt_state == BT_STATE_CONNECTED) {
    tp_canvas->setTextSize(1);
    tp_canvas->setTextColor(TP_CLR_GREEN);
    tp_canvas->setCursor(cx - 39, TP_WF_Y + 68);
    tp_canvas->print("BLE connected");
    tp_canvas->setTextColor(TP_CLR_TEXT_DIM);
    tp_canvas->setCursor(cx - 47, TP_WF_Y + 84);
    tp_canvas->print("waiting for host");
    return;
  }

  // Idle: pairing hint panel (no touch — encoder hold arms pairing)
  int bw = 130; int bh = 56;
  int bx = TP_WF_X + (TP_WF_W - bw) / 2;
  int by = TP_WF_Y + 56;
  tp_canvas->fillRoundRect(bx, by, bw, bh, 8, TP_CLR_BG_PANEL);
  tp_canvas->drawRoundRect(bx, by, bw, bh, 8, TP_CLR_ACCENT);
  tp_canvas->setTextSize(1);
  tp_canvas->setTextColor(TP_CLR_ACCENT);
  tp_canvas->setCursor(bx + 29, by + 16);
  tp_canvas->print("Hold knob 5s");
  tp_canvas->setCursor(bx + 32, by + 32);
  tp_canvas->print("to pair BLE");
}

void tp_draw_waterfall() {
  tp_canvas->fillRect(TP_WF_X, TP_WF_Y, TP_WF_W, TP_WF_H + 2, TP_CLR_BG);
  tp_canvas->drawFastVLine(TP_WF_X - 2, TP_WF_Y, TP_WF_H, TP_CLR_BORDER);

  if (!radio_online) {
    tp_draw_pair_panel();
    return;
  }

  int rssi_val = current_rssi;
  if (rssi_val < WF_RSSI_MIN) rssi_val = WF_RSSI_MIN;
  if (rssi_val > WF_RSSI_MAX) rssi_val = WF_RSSI_MAX;
  int rssi_norm = ((rssi_val - WF_RSSI_MIN) * 8) / WF_RSSI_SPAN;
  if (rssi_norm > 7) rssi_norm = 7;
  if (rssi_norm < 0) rssi_norm = 0;

  if (display_tx) {
    for (uint8_t i = 0; i < WF_TX_SIZE; i++) {
      tp_waterfall[tp_waterfall_head] = -1;
      tp_waterfall_head = (tp_waterfall_head + 1) % TP_WF_SIZE;
    }
    display_tx = false;
  } else {
    tp_waterfall[tp_waterfall_head] = interference_detected ? 8 : rssi_norm;
    tp_waterfall_head = (tp_waterfall_head + 1) % TP_WF_SIZE;
  }

  for (int i = 0; i < TP_WF_SIZE; i++) {
    int wi = (tp_waterfall_head + i) % TP_WF_SIZE;
    int ws = tp_waterfall[wi];
    int ypos = TP_WF_Y + i;
    if (ws >= 0) {
      int bar_w = (ws * (TP_WF_W - 4)) / 8;
      if (bar_w > 0) tp_canvas->drawFastHLine(TP_WF_X + 1, ypos, bar_w, tp_wf_palette[ws]);
    } else {
      for (int tx = 0; tx < TP_WF_W - 4; tx += 2) {
        tp_canvas->drawPixel(TP_WF_X + 1 + tx, ypos, 0xFFFF);
      }
    }
  }
}

void tp_draw_idle_content() {
  int x = 8;
  int y = TP_CONTENT_Y + 2;

  tp_canvas->setTextSize(2);
  tp_canvas->setTextColor(TP_CLR_TEXT_PRIMARY);
  tp_canvas->setCursor(x, y);
  if (bt_devname[0] != 0) tp_canvas->print(bt_devname);
  else tp_canvas->print("RNode");
  y += 20;

  char idbuf[8];
  snprintf(idbuf, sizeof(idbuf), "%02X%02X", (uint8_t)bt_dh[14], (uint8_t)bt_dh[15]);
  tp_canvas->setTextSize(5);
  tp_canvas->setTextColor(TP_CLR_ACCENT);
  tp_canvas->setCursor(x, y);
  tp_canvas->print(idbuf);
  y += 44;

  uint32_t freq_hz = (radio_online && LoRa != nullptr) ? LoRa->getFrequency() : lora_freq;
  char fbuf[20];
  snprintf(fbuf, sizeof(fbuf), "%.3f MHz", (float)freq_hz / 1000000.0f);
  tp_canvas->setTextSize(2);
  tp_canvas->setTextColor(TP_CLR_TEXT_PRIMARY);
  tp_canvas->setCursor(x, y);
  tp_canvas->print(fbuf);
  y += 20;

  uint32_t bw_hz = lora_bw;
  if (bw_hz == 0 && radio_online && LoRa != nullptr) bw_hz = LoRa->getSignalBandwidth();
  char bwstr[10];
  if (bw_hz >= 1000) snprintf(bwstr, sizeof(bwstr), "%luK", (unsigned long)(bw_hz / 1000));
  else snprintf(bwstr, sizeof(bwstr), "%lu", (unsigned long)bw_hz);
  char modbuf[36];
  int txp = (radio_online && LoRa != nullptr) ? (int)LoRa->getTxPower() : (int)lora_txp;
  snprintf(modbuf, sizeof(modbuf), "SF%d %s CR4/%d %+ddBm", lora_sf, bwstr, lora_cr, txp);
  tp_canvas->setTextSize(1);
  tp_canvas->setTextColor(TP_CLR_TEXT_SECONDARY);
  tp_canvas->setCursor(x, y);
  tp_canvas->print(modbuf);
  y += 12;

  if (radio_online) {
    char brbuf[24];
    snprintf(brbuf, sizeof(brbuf), "%.2f Kbps", (float)lora_bitrate / 1000.0f);
    tp_canvas->setCursor(x, y);
    tp_canvas->print(brbuf);
  }
  y += 12;

  tp_canvas->setCursor(x, y);
  if (radio_online) {
    tp_canvas->setTextColor(TP_CLR_GREEN);
    tp_canvas->print("Radio Online");
  } else {
    tp_canvas->setTextColor(TP_CLR_TEXT_SECONDARY);
    tp_canvas->print("Ready to connect.");
  }
  y += 14;

  tp_canvas->drawFastHLine(x, y, TP_LEFT_W - 16, TP_CLR_BORDER);
  y += 5;

  float at_st = airtime * 100.0f;
  float at_lt = longterm_airtime * 100.0f;
  float cl_st = total_channel_util * 100.0f;
  float cl_lt = longterm_channel_util * 100.0f;
  char abuf[40];
  tp_canvas->setTextColor(tp_load_color(at_st));
  snprintf(abuf, sizeof(abuf), "Air  ST %.1f%%  LT %.1f%%", at_st, at_lt);
  tp_canvas->setCursor(x, y);
  tp_canvas->print(abuf);
  y += 12;
  tp_canvas->setTextColor(tp_load_color(cl_st));
  snprintf(abuf, sizeof(abuf), "Chan ST %.1f%%  LT %.1f%%", cl_st, cl_lt);
  tp_canvas->setCursor(x, y);
  tp_canvas->print(abuf);
  y += 14;

  // last_rssi is -292 (sentinel) until a packet has actually been received
  char sigbuf[32];
  float rssi_pct = 0.0f;
  if (radio_online && last_rssi > -200) {
    signed char t_snr = (signed char)last_snr_raw;
    float snr = ((int)t_snr) * 0.25f;
    snprintf(sigbuf, sizeof(sigbuf), "RSSI %d   SNR %.1f", last_rssi, snr);
    rssi_pct = ((float)(last_rssi - S_RSSI_MIN) / S_RSSI_SPAN) * 100.0f;
  } else {
    snprintf(sigbuf, sizeof(sigbuf), "RSSI --   SNR --");
  }
  tp_canvas->setTextColor(TP_CLR_TEXT_PRIMARY);
  tp_canvas->setCursor(x, y);
  tp_canvas->print(sigbuf);
  y += 11;

  tp_draw_gradient_bar(x, y, TP_LEFT_W - 16, 10, rssi_pct);
}

void tp_draw_pairing_screen() {
  int x = 8;
  int y = TP_CONTENT_Y + 8;

  tp_canvas->setTextSize(2);
  tp_canvas->setTextColor(TP_CLR_WARN);
  tp_canvas->setCursor(x, y);
  tp_canvas->print("BLE Pairing");
  y += 28;

  if (bt_ssp_pin != 0) {
    char pin_str[7];
    snprintf(pin_str, sizeof(pin_str), "%06d", (int)bt_ssp_pin);
    int dx = x;
    for (int i = 0; i < 6; i++) {
      tp_canvas->fillRoundRect(dx, y, 36, 52, 5, TP_CLR_BG_PANEL);
      tp_canvas->drawRoundRect(dx, y, 36, 52, 5, TP_CLR_ACCENT);
      tp_canvas->setTextSize(4);
      tp_canvas->setTextColor(TP_CLR_ACCENT);
      tp_canvas->setCursor(dx + 6, y + 10);
      tp_canvas->print(pin_str[i]);
      dx += 42;
    }
    y += 64;
  }
}

void tp_draw_error_screen() {
  int x = 8;
  int y = TP_CONTENT_Y + 10;

  tp_canvas->setTextSize(2);
  tp_canvas->setTextColor(TP_CLR_ERROR);
  tp_canvas->setCursor(x, y);

  if (!device_firmware_ok()) {
    tp_canvas->print("FW Corrupt");
    y += 26;
    tp_canvas->setTextSize(1);
    tp_canvas->setTextColor(TP_CLR_TEXT_SECONDARY);
    tp_canvas->setCursor(x, y);
    tp_canvas->print("Reflash firmware to fix");
  } else if (!modem_installed) {
    tp_canvas->print("No Radio");
    y += 26;
    tp_canvas->setTextSize(1);
    tp_canvas->setTextColor(TP_CLR_TEXT_SECONDARY);
    tp_canvas->setCursor(x, y);
    tp_canvas->print("Check radio module");
  } else {
    tp_canvas->print("Config Error");
    y += 26;
    tp_canvas->setTextSize(1);
    tp_canvas->setTextColor(TP_CLR_TEXT_SECONDARY);
    tp_canvas->setCursor(x, y);
    tp_canvas->print("Run rnodeconf");
  }
}

void tp_draw_external_framebuffer() {
  int scale = 2;
  int ox = (TP_LEFT_W - 64 * scale) / 2;
  int oy = TP_CONTENT_Y + 22;
  tp_canvas->drawRect(ox - 2, oy - 2, 64 * scale + 4, 64 * scale + 4, TP_CLR_BORDER);
  for (int row = 0; row < 64; row++) {
    for (int col = 0; col < 64; col++) {
      int index = row * 8 + (col / 8);
      uint8_t bitmask = 1 << (7 - (col % 8));
      if (fb[index] & bitmask) {
        tp_canvas->fillRect(ox + col * scale, oy + row * scale, scale, scale, 0xFFFF);
      }
    }
  }
}

void tp_draw_boot_screen() {
  tp_canvas->fillScreen(TP_CLR_BG);
  tp_canvas->setTextSize(4);
  tp_canvas->setTextColor(TP_CLR_ACCENT);
  tp_canvas->setCursor(180, 70);
  tp_canvas->print("RNode");
  tp_canvas->setTextSize(1);
  tp_canvas->setTextColor(TP_CLR_TEXT_SECONDARY);
  tp_canvas->setCursor(192, 112);
  tp_canvas->print("rsPager  T-Pager");

  int bar_w = 200;
  int bar_x = (TP_W - bar_w) / 2;
  int bar_y = 140;
  tp_canvas->drawRect(bar_x, bar_y, bar_w, 8, TP_CLR_BORDER);
  int pulse = (millis() / 15) % bar_w;
  int pw = 50;
  int p_start = pulse - pw; if (p_start < 0) p_start = 0;
  int p_end = pulse; if (p_end > bar_w - 2) p_end = bar_w - 2;
  if (p_end > p_start) {
    tp_canvas->fillRect(bar_x + 1 + p_start, bar_y + 1, p_end - p_start, 6, TP_CLR_ACCENT);
  }
}

void tp_draw_footer() {
  tp_canvas->drawFastHLine(0, TP_FOOT_Y, TP_W, TP_CLR_BORDER);
  tp_canvas->setTextSize(1);
  tp_canvas->setTextColor(TP_CLR_TEXT_DIM);
  tp_canvas->setCursor(8, TP_FOOT_Y + 4);
  char vbuf[20];
  snprintf(vbuf, sizeof(vbuf), "Firmware v%d.%02d", MAJ_VERS, MIN_VERS);
  tp_canvas->print(vbuf);
}

void tp_render_frame() {
  if (!tp_ui_ready) tp_ui_init();
  if (!tp_canvas || !tp_canvas->getBuffer()) return;

  if (!device_init_done) {
    tp_draw_boot_screen();
    return;
  }

  if (firmware_update_mode) {
    tp_canvas->fillScreen(TP_CLR_BG);
    tp_canvas->setTextSize(2);
    tp_canvas->setTextColor(TP_CLR_WARN);
    tp_canvas->setCursor(150, 90);
    tp_canvas->print("Firmware Update");
    tp_canvas->setTextSize(1);
    tp_canvas->setTextColor(TP_CLR_ERROR);
    tp_canvas->setCursor(192, 120);
    tp_canvas->print("Do not power off");
    return;
  }

  tp_canvas->fillScreen(TP_CLR_BG);
  tp_draw_statusbar();
  tp_draw_waterfall();
  tp_draw_footer();

  if (disp_ext_fb && bt_ssp_pin == 0) {
    tp_draw_external_framebuffer();
  } else if (!hw_ready || radio_error || !device_firmware_ok()) {
    tp_draw_error_screen();
  } else if (bt_state == BT_STATE_PAIRING && bt_ssp_pin != 0) {
    tp_draw_pairing_screen();
  } else {
    tp_draw_idle_content();
  }
}

#endif // TPAGER_UI_H
