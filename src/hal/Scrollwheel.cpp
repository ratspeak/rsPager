#include "Scrollwheel.h"

volatile int8_t Scrollwheel::_deltaX = 0;
volatile int8_t Scrollwheel::_deltaY = 0;
volatile uint8_t Scrollwheel::_qState = 0;
volatile bool Scrollwheel::_clickFlag = false;
Scrollwheel* Scrollwheel::_instance = nullptr;

bool Scrollwheel::begin() {
    _instance = this;

    // T-Pager exposes a vertical rotary encoder only: up/down plus center click.
    pinMode(ROTARY_A, INPUT_PULLUP);
    pinMode(ROTARY_B, INPUT_PULLUP);
    pinMode(ROTARY_CLICK, INPUT_PULLUP);
    _qState = 0;  // R_START; the state machine self-syncs from any pin state

    attachInterrupt(digitalPinToInterrupt(ROTARY_A), isrEncoder, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ROTARY_B), isrEncoder, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ROTARY_CLICK), isrClick, FALLING);

    Serial.println("[ENCODER] Initialized (vertical + click)");
    return true;
}

void Scrollwheel::update() {
    noInterrupts();
    int8_t dx = _deltaX;
    int8_t dy = _deltaY;
    bool click = _clickFlag;
    _deltaX = 0;
    _deltaY = 0;
    _clickFlag = false;
    interrupts();

    _lastDX = dx;
    _lastDY = dy;

    _cursorX += dx * _speed;
    _cursorY += dy * _speed;

    if (_cursorX < 0) _cursorX = 0;
    if (_cursorX >= TFT_WIDTH) _cursorX = TFT_WIDTH - 1;
    if (_cursorY < 0) _cursorY = 0;
    if (_cursorY >= TFT_HEIGHT) _cursorY = TFT_HEIGHT - 1;

    _clicked = click;
    _hadMovement = (dx != 0 || dy != 0);
}

void IRAM_ATTR Scrollwheel::isrEncoder() {
    // Buxton full-step quadrature state machine — the exact decode LilyGoLib
    // ships for this board (src/rotary/Rotary.cpp, HALF_STEP disabled): one
    // step per detent, emitted on arriving back at AB=11 after a valid
    // 4-transition gray sequence; any out-of-order transition resets to start
    // without emitting, which inherently debounces. Pin packing matches their
    // Rotary(ROTARY_A, ROTARY_B): pinstate = B<<1 | A. Horizontal delta stays
    // zero: the T-Pager wheel has no left/right axis.
    constexpr uint8_t DIR_CW  = 0x10;
    constexpr uint8_t DIR_CCW = 0x20;
    // States: 0 START, 1 CW_FINAL, 2 CW_BEGIN, 3 CW_NEXT,
    //         4 CCW_BEGIN, 5 CCW_FINAL, 6 CCW_NEXT. Columns: AB 00,01,10,11.
    static constexpr uint8_t table[7][4] = {
        {0x0, 0x2, 0x4, 0x0},
        {0x3, 0x0, 0x1, 0x0 | DIR_CW},
        {0x3, 0x2, 0x0, 0x0},
        {0x3, 0x2, 0x1, 0x0},
        {0x6, 0x0, 0x4, 0x0},
        {0x6, 0x5, 0x0, 0x0 | DIR_CCW},
        {0x6, 0x5, 0x4, 0x0},
    };
    uint8_t pinstate = (digitalRead(ROTARY_B) ? 0x02 : 0) | (digitalRead(ROTARY_A) ? 0x01 : 0);
    _qState = table[_qState & 0x0F][pinstate];
    uint8_t dir = _qState & 0x30;
    if (dir == DIR_CCW)     _deltaY -= 1;  // A leads = up (existing sign convention)
    else if (dir == DIR_CW) _deltaY += 1;  // B leads = down
}

void IRAM_ATTR Scrollwheel::isrClick() { _clickFlag = true; }
