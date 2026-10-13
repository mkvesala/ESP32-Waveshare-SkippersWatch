#include "TouchInput.h"
#include <Wire.h>

// === P U B L I C ===

TouchInput::TouchInput() {}

void TouchInput::begin() {
    if (_initialized) return;

    // The app has already begun Wire on the shared bus (SDA=15/SCL=14, with the PMIC).
    // The driver does its own reset pulse and ID probe.
    _available = _driver.begin(Wire, TOUCH_RST_PIN);
    if (!_available) {
        Serial.println("WARN: FT3168 touch not detected");
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

void TouchInput::hibernate() {
    if (!_initialized || !_available) return;
    _driver.hibernate();
}

// === P R I V A T E ===

// Classify the just-ended touch. Held duration excludes the release grace (measured to
// the last fresh point). The rules live in GestureClassifier (host-testable).
void TouchInput::classifyRelease() {
    _gesture = GestureClassifier::classify(_down_x, _down_y, _x, _y, _max_move,
                                           _last_point_ms - _down_ms);
}

bool TouchInput::readRawTouch(int16_t& x, int16_t& y) {
    uint16_t rx, ry;
    if (!_driver.readPoint(rx, ry)) return false;
    if (rx >= PANEL_WIDTH)  rx = PANEL_WIDTH - 1;
    if (ry >= PANEL_HEIGHT) ry = PANEL_HEIGHT - 1;
    x = (int16_t)rx;
    y = (int16_t)ry;
    return true;
}
