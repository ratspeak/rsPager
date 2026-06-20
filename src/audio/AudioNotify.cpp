// Audio output for T-Pager via I2S codec/amplifier path
#include "AudioNotify.h"
#include "config/BoardConfig.h"
#include <Wire.h>
#include <driver/i2s.h>
#include <math.h>

#define AUDIO_SAMPLE_RATE  16000
#define I2S_PORT           I2S_NUM_0

namespace {
constexpr uint8_t ES8311_ADDR = 0x18;

constexpr uint8_t ES8311_RESET_REG00       = 0x00;
constexpr uint8_t ES8311_CLK_MANAGER_REG01 = 0x01;
constexpr uint8_t ES8311_CLK_MANAGER_REG02 = 0x02;
constexpr uint8_t ES8311_CLK_MANAGER_REG03 = 0x03;
constexpr uint8_t ES8311_CLK_MANAGER_REG04 = 0x04;
constexpr uint8_t ES8311_CLK_MANAGER_REG05 = 0x05;
constexpr uint8_t ES8311_CLK_MANAGER_REG06 = 0x06;
constexpr uint8_t ES8311_CLK_MANAGER_REG07 = 0x07;
constexpr uint8_t ES8311_CLK_MANAGER_REG08 = 0x08;
constexpr uint8_t ES8311_SDPIN_REG09       = 0x09;
constexpr uint8_t ES8311_SDPOUT_REG0A      = 0x0A;
constexpr uint8_t ES8311_SYSTEM_REG0B      = 0x0B;
constexpr uint8_t ES8311_SYSTEM_REG0C      = 0x0C;
constexpr uint8_t ES8311_SYSTEM_REG0D      = 0x0D;
constexpr uint8_t ES8311_SYSTEM_REG0E      = 0x0E;
constexpr uint8_t ES8311_SYSTEM_REG10      = 0x10;
constexpr uint8_t ES8311_SYSTEM_REG11      = 0x11;
constexpr uint8_t ES8311_SYSTEM_REG12      = 0x12;
constexpr uint8_t ES8311_SYSTEM_REG13      = 0x13;
constexpr uint8_t ES8311_SYSTEM_REG14      = 0x14;
constexpr uint8_t ES8311_ADC_REG15         = 0x15;
constexpr uint8_t ES8311_ADC_REG16         = 0x16;
constexpr uint8_t ES8311_ADC_REG17         = 0x17;
constexpr uint8_t ES8311_ADC_REG1B         = 0x1B;
constexpr uint8_t ES8311_ADC_REG1C         = 0x1C;
constexpr uint8_t ES8311_DAC_REG31         = 0x31;
constexpr uint8_t ES8311_DAC_REG32         = 0x32;
constexpr uint8_t ES8311_DAC_REG37         = 0x37;
constexpr uint8_t ES8311_GPIO_REG44        = 0x44;
constexpr uint8_t ES8311_GP_REG45          = 0x45;

uint8_t codecVolumeReg(uint8_t volume) {
    if (volume == 0) return 0x00;
    volume = constrain(volume, 1, 100);
    return 0x80 + ((uint16_t)volume * 0x40 / 100);
}
}

void AudioNotify::begin() {
    i2s_config_t i2s_config = {};
    i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    i2s_config.sample_rate = AUDIO_SAMPLE_RATE;
    i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    i2s_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2s_config.dma_buf_count = 4;
    i2s_config.dma_buf_len = 256;
    i2s_config.use_apll = false;
    i2s_config.tx_desc_auto_clear = true;

    i2s_pin_config_t pin_config = {};
    pin_config.mck_io_num = I2S_MCLK;
    pin_config.bck_io_num = I2S_BCK;
    pin_config.ws_io_num = I2S_WS;
    pin_config.data_out_num = I2S_DOUT;
    pin_config.data_in_num = I2S_PIN_NO_CHANGE;

    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[AUDIO] I2S install failed: %d\n", err);
        return;
    }

    err = i2s_set_pin(I2S_PORT, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("[AUDIO] I2S pin config failed: %d\n", err);
        i2s_driver_uninstall(I2S_PORT);
        return;
    }

    i2s_zero_dma_buffer(I2S_PORT);
    _i2sReady = true;
    Serial.println("[AUDIO] I2S initialized");

    _codecReady = beginCodec();
    if (!_codecReady) {
        Serial.println("[AUDIO] ES8311 codec init failed");
    }
}

void AudioNotify::end() {
    if (_codecReady) {
        setCodecMute(true);
        _codecReady = false;
    }
    if (_i2sReady) {
        i2s_driver_uninstall(I2S_PORT);
        _i2sReady = false;
    }
}

void AudioNotify::setEnabled(bool enabled) {
    _enabled = enabled;
    if (_codecReady) setCodecMute(!enabled || _volume == 0);
}

void AudioNotify::setVolume(uint8_t vol) {
    _volume = constrain(vol, 0, 100);
    if (_codecReady) {
        setCodecVolume(_volume);
        setCodecMute(!_enabled || _volume == 0);
    }
}

bool AudioNotify::writeCodecReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool AudioNotify::readCodecReg(uint8_t reg, uint8_t& value) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1) != 1) return false;
    value = Wire.read();
    return true;
}

bool AudioNotify::updateCodecReg(uint8_t reg, uint8_t clearMask, uint8_t setMask) {
    uint8_t value = 0;
    if (!readCodecReg(reg, value)) return false;
    value &= ~clearMask;
    value |= setMask;
    return writeCodecReg(reg, value);
}

bool AudioNotify::configureCodecSampleRate() {
    bool ok = true;

    ok &= updateCodecReg(ES8311_SDPIN_REG09, 0x03, 0x0C);   // I2S, 16-bit DAC input
    ok &= updateCodecReg(ES8311_SDPOUT_REG0A, 0x03, 0x0C);  // I2S, 16-bit ADC output

    // 16 kHz with 256x MCLK = 4.096 MHz. Matches LilyGoLib's ES8311 coeff table.
    ok &= updateCodecReg(ES8311_CLK_MANAGER_REG02, 0xF8, 0x00);
    ok &= writeCodecReg(ES8311_CLK_MANAGER_REG05, 0x00);
    ok &= updateCodecReg(ES8311_CLK_MANAGER_REG03, 0x7F, 0x10);
    ok &= updateCodecReg(ES8311_CLK_MANAGER_REG04, 0x7F, 0x20);
    ok &= updateCodecReg(ES8311_CLK_MANAGER_REG07, 0x3F, 0x00);
    ok &= writeCodecReg(ES8311_CLK_MANAGER_REG08, 0xFF);
    ok &= updateCodecReg(ES8311_CLK_MANAGER_REG06, 0x1F, 0x03);

    return ok;
}

bool AudioNotify::beginCodec() {
    Wire.beginTransmission(ES8311_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.printf("[AUDIO] ES8311 not found at 0x%02X\n", ES8311_ADDR);
        return false;
    }

    bool ok = true;
    ok &= writeCodecReg(ES8311_GPIO_REG44, 0x08);
    ok &= writeCodecReg(ES8311_GPIO_REG44, 0x08);
    ok &= writeCodecReg(ES8311_CLK_MANAGER_REG01, 0x30);
    ok &= writeCodecReg(ES8311_CLK_MANAGER_REG02, 0x00);
    ok &= writeCodecReg(ES8311_CLK_MANAGER_REG03, 0x10);
    ok &= writeCodecReg(ES8311_ADC_REG16, 0x24);
    ok &= writeCodecReg(ES8311_CLK_MANAGER_REG04, 0x10);
    ok &= writeCodecReg(ES8311_CLK_MANAGER_REG05, 0x00);
    ok &= writeCodecReg(ES8311_SYSTEM_REG0B, 0x00);
    ok &= writeCodecReg(ES8311_SYSTEM_REG0C, 0x00);
    ok &= writeCodecReg(ES8311_SYSTEM_REG10, 0x1F);
    ok &= writeCodecReg(ES8311_SYSTEM_REG11, 0x7F);
    ok &= writeCodecReg(ES8311_RESET_REG00, 0x80);
    ok &= updateCodecReg(ES8311_RESET_REG00, 0x40, 0x00);       // slave mode
    ok &= writeCodecReg(ES8311_CLK_MANAGER_REG01, 0x3F);       // use external MCLK
    ok &= updateCodecReg(ES8311_CLK_MANAGER_REG06, 0x20, 0x00);
    ok &= writeCodecReg(ES8311_SYSTEM_REG13, 0x10);
    ok &= writeCodecReg(ES8311_ADC_REG1B, 0x0A);
    ok &= writeCodecReg(ES8311_ADC_REG1C, 0x6A);
    ok &= writeCodecReg(ES8311_GPIO_REG44, 0x58);
    ok &= configureCodecSampleRate();

    ok &= writeCodecReg(ES8311_RESET_REG00, 0x80);
    ok &= writeCodecReg(ES8311_CLK_MANAGER_REG01, 0x3F);
    ok &= updateCodecReg(ES8311_SDPIN_REG09, 0x40, 0x00);
    ok &= updateCodecReg(ES8311_SDPOUT_REG0A, 0x40, 0x00);
    ok &= writeCodecReg(ES8311_ADC_REG17, 0xBF);
    ok &= writeCodecReg(ES8311_SYSTEM_REG0E, 0x02);
    ok &= writeCodecReg(ES8311_SYSTEM_REG12, 0x00);
    ok &= writeCodecReg(ES8311_SYSTEM_REG14, 0x1A);
    ok &= updateCodecReg(ES8311_SYSTEM_REG14, 0x40, 0x00);
    ok &= writeCodecReg(ES8311_SYSTEM_REG0D, 0x01);
    ok &= writeCodecReg(ES8311_ADC_REG15, 0x40);
    ok &= writeCodecReg(ES8311_DAC_REG37, 0x08);
    ok &= writeCodecReg(ES8311_GP_REG45, 0x00);
    ok &= setCodecVolume(_volume);
    ok &= setCodecMute(!_enabled || _volume == 0);

    if (ok) Serial.println("[AUDIO] ES8311 codec initialized");
    return ok;
}

bool AudioNotify::setCodecMute(bool mute) {
    return updateCodecReg(ES8311_DAC_REG31, 0x60, mute ? 0x60 : 0x00);
}

bool AudioNotify::setCodecVolume(uint8_t volume) {
    return writeCodecReg(ES8311_DAC_REG32, codecVolumeReg(volume));
}

void AudioNotify::writeTone(uint16_t freq, uint16_t durationMs) {
    if (!canPlay()) return;

    int numSamples = (AUDIO_SAMPLE_RATE * durationMs) / 1000;
    int16_t* buf = (int16_t*)ps_malloc(numSamples * sizeof(int16_t));
    if (!buf) buf = (int16_t*)malloc(numSamples * sizeof(int16_t));
    if (!buf) return;

    float vol = (_volume / 100.0f) * 16000.0f;
    int fadeN = AUDIO_SAMPLE_RATE / 100; // 10ms fade

    for (int i = 0; i < numSamples; i++) {
        float t = (float)i / AUDIO_SAMPLE_RATE;
        // Fundamental + 2nd/3rd harmonics for warmth
        float s = sinf(2.0f * M_PI * freq * t) * 0.70f
                + sinf(2.0f * M_PI * freq * 2.0f * t) * 0.20f
                + sinf(2.0f * M_PI * freq * 3.0f * t) * 0.10f;
        // Fade envelope
        float env = 1.0f;
        if (i < fadeN) env = (float)i / fadeN;
        if (i > numSamples - fadeN) env = (float)(numSamples - i) / fadeN;
        buf[i] = (int16_t)(s * env * vol);
    }

    size_t written = 0;
    i2s_write(I2S_PORT, buf, numSamples * sizeof(int16_t), &written, pdMS_TO_TICKS(200));
    free(buf);
}

void AudioNotify::writeSilence(uint16_t durationMs) {
    if (!_i2sReady || !_codecReady) return;
    int numSamples = (AUDIO_SAMPLE_RATE * durationMs) / 1000;
    size_t bufSize = numSamples * sizeof(int16_t);
    int16_t* buf = (int16_t*)ps_malloc(bufSize);
    if (!buf) buf = (int16_t*)malloc(bufSize);
    if (!buf) return;
    memset(buf, 0, bufSize);
    size_t written = 0;
    i2s_write(I2S_PORT, buf, numSamples * sizeof(int16_t), &written, pdMS_TO_TICKS(200));
    free(buf);
}

void AudioNotify::playMessage() {
    if (!canPlay()) return;

    const int sr = AUDIO_SAMPLE_RATE;
    const int toneMs = 34;
    const int gapMs = 28;
    const int tailMs = 16;
    const int totalMs = toneMs + gapMs + toneMs + tailMs;
    const int totalSamples = sr * totalMs / 1000;

    int16_t* buf = (int16_t*)ps_malloc(totalSamples * sizeof(int16_t));
    if (!buf) buf = (int16_t*)malloc(totalSamples * sizeof(int16_t));
    if (!buf) return;
    memset(buf, 0, totalSamples * sizeof(int16_t));

    float vol = (_volume / 100.0f) * 12000.0f;
    int pos = 0;

    auto addTone = [&](float freq, int ms) {
        int n = sr * ms / 1000;
        int fadeN = sr * 5 / 1000;
        if (fadeN < 1) fadeN = 1;
        for (int i = 0; i < n && (pos + i) < totalSamples; i++) {
            float t = (float)i / sr;
            float s = sinf(2.0f * M_PI * freq * t) * 0.80f
                    + sinf(2.0f * M_PI * freq * 2.0f * t) * 0.15f
                    + sinf(2.0f * M_PI * freq * 3.0f * t) * 0.05f;
            float env = 1.0f;
            if (i < fadeN) env = (float)i / fadeN;
            if (i > n - fadeN) env = (float)(n - i) / fadeN;
            buf[pos + i] = (int16_t)(s * env * vol);
        }
        pos += n;
    };

    addTone(1000.0f, toneMs);
    pos += sr * gapMs / 1000;
    addTone(1000.0f, toneMs);
    pos += sr * tailMs / 1000;

    size_t written = 0;
    i2s_write(I2S_PORT, buf, totalSamples * sizeof(int16_t), &written, pdMS_TO_TICKS(150));
    free(buf);
}

void AudioNotify::requestMessage() {
    if (!_enabled) return;
    _messagePending = true;
}

void AudioNotify::loop() {
    if (!_messagePending) return;
    _messagePending = false;
    playMessage();
}

void AudioNotify::playAnnounce() {
    if (!_enabled) return;
    writeTone(800, 30);
    writeSilence(20);
}

void AudioNotify::playError() {
    if (!_enabled) return;
    for (int i = 0; i < 3; i++) {
        writeTone(400, 100);
        if (i < 2) writeSilence(50);
    }
    writeSilence(30);
}

void AudioNotify::playBoot() {
    if (!canPlay()) return;

    // === RATDECK BOOT SEQUENCE ===
    // Sci-fi computer startup: sweep -> digital arpeggio -> confirmation
    // Total ~550ms

    const int sr = AUDIO_SAMPLE_RATE;
    const int totalMs = 560;
    const int totalSamples = sr * totalMs / 1000;

    int16_t* buf = (int16_t*)ps_malloc(totalSamples * sizeof(int16_t));
    if (!buf) {
        buf = (int16_t*)malloc(totalSamples * sizeof(int16_t));
        if (!buf) return;
    }
    memset(buf, 0, totalSamples * sizeof(int16_t));

    float vol = (_volume / 100.0f) * 16000.0f;
    int pos = 0;

    // Helper: add a tone with harmonics at current position
    auto addTone = [&](float freq, int ms) {
        int n = sr * ms / 1000;
        int fadeN = sr * 8 / 1000; // 8ms fade
        for (int i = 0; i < n && (pos + i) < totalSamples; i++) {
            float t = (float)i / sr;
            float s = sinf(2.0f * M_PI * freq * t) * 0.65f
                    + sinf(2.0f * M_PI * freq * 2.0f * t) * 0.22f
                    + sinf(2.0f * M_PI * freq * 3.0f * t) * 0.13f;
            float env = 1.0f;
            if (i < fadeN) env = (float)i / fadeN;
            if (i > n - fadeN) env = (float)(n - i) / fadeN;
            buf[pos + i] = (int16_t)(s * env * vol);
        }
        pos += n;
    };

    // Helper: frequency sweep with harmonics
    auto addSweep = [&](float startF, float endF, int ms) {
        int n = sr * ms / 1000;
        int fadeN = sr * 8 / 1000;
        float phase = 0;
        for (int i = 0; i < n && (pos + i) < totalSamples; i++) {
            float t = (float)i / n; // 0..1 progress
            float freq = startF + (endF - startF) * t * t; // quadratic sweep (accelerating)
            phase += 2.0f * M_PI * freq / sr;
            float s = sinf(phase) * 0.65f
                    + sinf(phase * 2.0f) * 0.22f
                    + sinf(phase * 3.0f) * 0.08f;
            float env = 1.0f;
            if (i < fadeN) env = (float)i / fadeN;
            if (i > n - fadeN) env = (float)(n - i) / fadeN;
            buf[pos + i] = (int16_t)(s * env * vol);
        }
        pos += n;
    };

    auto addSilence = [&](int ms) {
        pos += sr * ms / 1000;
    };

    // Phase 1: Rising power sweep 300->1200Hz (160ms) — "systems powering up"
    addSweep(300, 1200, 160);
    addSilence(25);

    // Phase 2: Three quick ascending staccato notes — E5, G#5, B5
    // (E major triad in 2nd inversion — bright, triumphant, slightly edgy)
    addTone(659,  45);   // E5
    addSilence(12);
    addTone(831,  45);   // G#5
    addSilence(12);
    addTone(988,  45);   // B5
    addSilence(25);

    // Phase 3: Descending glitch sweep 2400->1600Hz (60ms) — "digital handshake"
    addSweep(2400, 1600, 60);
    addSilence(20);

    // Phase 4: Final confirmation — E6 (1319Hz), 100ms with clean decay — "online"
    addTone(1319, 100);

    // Write entire sequence at once for seamless playback
    size_t written = 0;
    i2s_write(I2S_PORT, buf, pos * sizeof(int16_t), &written, pdMS_TO_TICKS(200));

    // Flush with silence
    memset(buf, 0, 512 * sizeof(int16_t));
    i2s_write(I2S_PORT, buf, 512 * sizeof(int16_t), &written, pdMS_TO_TICKS(200));

    free(buf);
}
