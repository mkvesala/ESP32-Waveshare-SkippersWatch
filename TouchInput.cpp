#include "TouchInput.h"
#include <Wire.h>

// === P U B L I C ===

TouchInput::TouchInput() {}

void TouchInput::begin() {
    if (_initialized) return;

    // The driver expects the I2C bus already configured; it then does its own reset
    // pulse, INT attach, and ID probe. SDA=4/SCL=3 is the shared bus (no other user yet).
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    _available = _driver.begin(Wire, TOUCH_RST_PIN, TOUCH_INT_PIN,
                               PANEL_WIDTH, PANEL_HEIGHT, PANEL_ROTATION);
    if (!_available) {
        Serial.println("WARN: AXS5106L touch not detected — encoder still active");
    }
    _initialized = true;
}

void TouchInput::poll(uint32_t now) {
    if (!_initialized || !_available) return;

    int16_t x, y;
    if (this->readRawTouch(x, y)) {
        // Fresh point: (re)assert the held state and refresh the liveness timestamp.
        _x = x;
        _y = y;
        _last_point_ms = now;
        if (!_down) {
            _down = true;
            _just_pressed = true;
            _down_x = x;
            _down_y = y;
            _down_ms = now;
            _max_move = 0;
        } else {
            // Track peak per-axis travel so a tap that jitters isn't seen as a swipe.
            const int16_t dx = (int16_t)abs((int)x - _down_x);
            const int16_t dy = (int16_t)abs((int)y - _down_y);
            const int16_t m = (dx > dy) ? dx : dy;
            if (m > _max_move) _max_move = m;
        }
    } else if (_down && (uint32_t)(now - _last_point_ms) >= RELEASE_GRACE_MS) {
        // No fresh point for the grace window → the finger has lifted.
        _down = false;
        _just_released = true;
        this->classifyRelease();
    }
}

bool TouchInput::justPressed() {
    const bool b = _just_pressed;
    _just_pressed = false;
    return b;
}

bool TouchInput::justReleased() {
    const bool b = _just_released;
    _just_released = false;
    return b;
}

TouchGesture TouchInput::getGesture() {
    const TouchGesture g = _gesture;
    _gesture = TouchGesture::NONE;
    return g;
}

// === P R I V A T E ===

// Classify the just-ended touch. Held duration excludes the release grace (measured to
// the last fresh point). Panel y grows downward, so a finger swiping UP produces a
// NEGATIVE net dy. Little travel → tap (or long-press by duration); enough vertical
// travel → swipe. Horizontal-only moves are ignored here (a horizontal drag on a
// control is claimed as a drag by the page, not classified as a gesture).
void TouchInput::classifyRelease() {
    if (_max_move <= TAP_MAX_MOVE_PX) {
        const uint32_t held = _last_point_ms - _down_ms;
        _gesture = (held >= LONG_PRESS_MS) ? TouchGesture::LONG_PRESS : TouchGesture::TAP;
        return;
    }

    const int16_t dy  = _y - _down_y;
    const int16_t adx = (int16_t)abs((int)_x - _down_x);
    const int16_t ady = (int16_t)abs((int)dy);

    if (ady >= adx && ady >= SWIPE_MIN_MOVE_PX) {
        _gesture = (dy < 0) ? TouchGesture::SWIPE_UP : TouchGesture::SWIPE_DOWN;
    } else {
        _gesture = TouchGesture::NONE;
    }
}

bool TouchInput::readRawTouch(int16_t& x, int16_t& y) {
    uint16_t rx, ry;
    if (!_driver.readPoint(rx, ry)) return false;
    x = (int16_t)rx;
    y = (int16_t)ry;
    return true;
}
