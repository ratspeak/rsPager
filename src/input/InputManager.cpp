#include "InputManager.h"
#include "config/BoardConfig.h"

void InputManager::begin(Keyboard* kb, Scrollwheel* sw, TouchInput* touch) {
    _kb = kb;
    _sw = sw;
    _touch = touch;
}

void InputManager::setScrollwheelSpeed(uint8_t speed) {
    if (speed < 1) speed = 1;
    if (speed > 5) speed = 5;
    _scrollwheelSpeed = speed;
}

int8_t InputManager::scrollwheelThreshold() const {
    static const int8_t thresholds[] = {5, 4, 3, 2, 1};
    return thresholds[_scrollwheelSpeed - 1];
}

unsigned long InputManager::scrollwheelRateMs() const {
    static const unsigned long rates[] = {260, 220, 180, 140, 100};
    return rates[_scrollwheelSpeed - 1];
}

void InputManager::update() {
    _hasKey = false;
    _activity = false;
    _strongActivity = false;
    _longPress = false;

    // Poll keyboard
    if (_kb) {
        _kb->update();
        if (_kb->hasEvent()) {
            _keyEvent = _kb->getEvent();
            _hasKey = true;
            _activity = true;
            _strongActivity = true;
        }
    }

    // Poll encoder — convert vertical deltas to nav KeyEvents
    if (_sw) {
        _sw->update();
        if (_sw->hadMovement()) {
            _activity = true;  // Movement is weak — only wakes from dim
        }

        // Generate nav events from encoder movement (click handled below via GPIO).
        // Skip entirely when screen is off so a backpacked device doesn't accumulate
        // phantom up/down/left/right keypresses or wake from movement.
        bool screenOn = !_powerMgr || _powerMgr->isScreenOn();
        if (!_hasKey && screenOn) {
            unsigned long now = millis();

            // Deltas arrive as whole detents (Scrollwheel ISR counts full
            // quadrature cycles). One detent = one nav step; consume one per
            // event and carry the rest so fast spins emit at the repeat rate
            // without ever double-stepping. Clamp queue to two detents so a
            // single click can never leave a long repeat-rate tail.
            _swAccumX += _sw->lastDeltaX();
            _swAccumY += _sw->lastDeltaY();
            if (_swAccumX > 2) _swAccumX = 2;
            if (_swAccumX < -2) _swAccumX = -2;
            if (_swAccumY > 2) _swAccumY = 2;
            if (_swAccumY < -2) _swAccumY = -2;

            if (now - _lastSwNavTime >= scrollwheelRateMs()) {
                int8_t absX = _swAccumX < 0 ? -_swAccumX : _swAccumX;
                int8_t absY = _swAccumY < 0 ? -_swAccumY : _swAccumY;
                bool yDominant = absY >= absX;

                if (yDominant && absY >= 1) {
                    _keyEvent = {};
                    if (_swAccumY < 0) _keyEvent.up = true;
                    else               _keyEvent.down = true;
                    _keyEvent.encoder = true;
                    _hasKey = true;
                    _activity = true;
                    _strongActivity = true;
                    _lastSwNavTime = now;
                    _swAccumY += (_swAccumY < 0) ? 1 : -1;
                    _swAccumX = 0;
                }
                else if (!yDominant && absX >= 1) {
                    _keyEvent = {};
                    if (_swAccumX < 0) _keyEvent.left = true;
                    else               _keyEvent.right = true;
                    _keyEvent.encoder = true;
                    _hasKey = true;
                    _activity = true;
                    _strongActivity = true;
                    _lastSwNavTime = now;
                    _swAccumX += (_swAccumX < 0) ? 1 : -1;
                    _swAccumY = 0;
                }
            }
        }

        // Click / long-press detection via GPIO (deferred click with debounce)
        // Short click fires on button RELEASE; long press fires after hold threshold
        // Debounce: require GPIO HIGH for CLICK_DEBOUNCE_MS before accepting release
        bool clickDown = (digitalRead(ROTARY_CLICK) == LOW);

        if (clickDown) {
            _lastClickDownMs = millis();  // Track last time GPIO was LOW
            if (!_clickPending) {
                // Button just went down — start tracking, don't fire yet
                _clickPending = true;
                _longPressFired = false;
                _clickStartMs = millis();
                // Capture screen state BEFORE activity wakes it, so long-press
                // doesn't blank a freshly-woken screen (wake-then-blank ping-pong)
                _clickFromScreenOn = _powerMgr ? _powerMgr->isScreenOn() : true;
                _activity = true;
                _strongActivity = true;  // Click wakes from screen off
            } else if (!_longPressFired && millis() - _clickStartMs >= LONG_PRESS_MS) {
                // Long press threshold reached — only emit if click started screen-on
                if (_clickFromScreenOn) {
                    _longPress = true;
                }
                _longPressFired = true;
                _hasKey = false;  // Suppress any concurrent events
                _activity = true;
                _strongActivity = true;
            }
        } else if (_clickPending) {
            // GPIO is HIGH — only accept release after debounce period
            if (millis() - _lastClickDownMs >= CLICK_DEBOUNCE_MS) {
                _clickPending = false;
                // Suppress wake-click: if the press began with the screen off,
                // the click's job was just to wake the device, not to confirm.
                if (!_longPressFired && !_hasKey && _clickFromScreenOn) {
                    _keyEvent = {};
                    _keyEvent.enter = true;
                    _keyEvent.character = '\n';
                    _keyEvent.encoder = true;
                    _hasKey = true;
                    _activity = true;
                    _strongActivity = true;
                    _lastSwNavTime = millis();
                    _swAccumX = 0;
                    _swAccumY = 0;
                }
                _longPressFired = false;
            }
            // If GPIO was LOW too recently, ignore — likely bounce
        }
    }

    // Touch activity check — compiled in for shared code paths; T-Pager passes null.
    if (_touch && (!_powerMgr || _powerMgr->isScreenOn())) {
        unsigned long now = millis();
        if (now - _lastTouchPoll >= TOUCH_POLL_MS) {
            _lastTouchPoll = now;
            _touch->update();
            if (_touch->isTouched()) {
                _activity = true;
                _strongActivity = true;
            }
        }
    }
}
