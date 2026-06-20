#pragma once

#include <Arduino.h>

class AudioNotify {
public:
    void begin();
    void end();

    // Notification sounds
    void playMessage();
    void playAnnounce();
    void playError();
    void playBoot();        // Sci-fi boot sequence
    void requestMessage();  // Defer short alert out of packet callbacks
    void loop();

    // Settings
    void setEnabled(bool enabled);
    bool isEnabled() const { return _enabled; }
    void setVolume(uint8_t vol);
    uint8_t volume() const { return _volume; }

private:
    bool canPlay() const { return _enabled && _i2sReady && _codecReady; }
    bool beginCodec();
    bool configureCodecSampleRate();
    bool writeCodecReg(uint8_t reg, uint8_t value);
    bool readCodecReg(uint8_t reg, uint8_t& value);
    bool updateCodecReg(uint8_t reg, uint8_t clearMask, uint8_t setMask);
    bool setCodecMute(bool mute);
    bool setCodecVolume(uint8_t volume);
    void writeTone(uint16_t freq, uint16_t durationMs);
    void writeSilence(uint16_t durationMs);

    bool _enabled = true;
    bool _i2sReady = false;
    bool _codecReady = false;
    volatile bool _messagePending = false;
    uint8_t _volume = 80;  // 0-100
};
