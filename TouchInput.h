#pragma once

#include <Arduino.h>
#include "axs5106l_touch.h"

// === C L A S S  T O U C H I N P U T ===
//
// - Manages the CrowPanel 2.01" Watch capacitive touch (AXS5106L) as a second input
//   device, parallel to the RotaryEncoder. Mirrors the encoder's read-and-clear API
//   so both feed the same IScreenUI dispatch in CrowPanelApplication — which is what
//   lets the encoder be removed later with no UI rewrite.
// - Init: _touch.begin();  Poll: _touch.poll(millis()) every loop.
// - No FreeRTOS task (unlike the encoder): touch has no tight-timing quadrature need,
//   and polling on the main loop avoids I2C concurrency with the shared bus.
// - Owned by: CrowPanelApplication.
// - Hardware (see hardware_notes.md): shared I2C SDA=4/SCL=3, INT=GPIO2, RST=GPIO5.
//
// Gestures are recognized in SOFTWARE from the touch-point stream (chip-agnostic).
// The AXS5106L driver (axs5106l_touch.*, ported from Elecrow Lesson03) is INTERRUPT/
// event-based: readPoint() returns a point only on a fresh INT and false otherwise —
// including between reports while a finger is held. poll() debounces that back into a
// level "held" state with a short release grace, so drags/swipes don't break up.

enum class TouchGesture : uint8_t {
    NONE = 0,
    TAP,          // brief contact, little travel → routed to the page's onButtonPress()
    SWIPE_UP,     // vertical drag, finger up   → next page (app-level)
    SWIPE_DOWN,   // vertical drag, finger down → previous page (app-level)
    LONG_PRESS,   // held with little travel — recognized but currently unbound (reserved)
};

class TouchInput {

public:

    explicit TouchInput();

    void begin();
    void poll(uint32_t now);       // call every loop

    // Low-level lifecycle for drag handling by the app (read-and-clear where noted).
    bool isDown() const { return _down; }
    bool justPressed();            // read-and-clear: a new touch-down since last poll
    bool justReleased();           // read-and-clear: the touch was released
    void point(int16_t& x, int16_t& y) const { x = _x; y = _y; }

    // Discrete gesture classified at release; read-and-clear.
    TouchGesture getGesture();

    // Touch INT — RTC-capable (GPIO2). Used by the driver as its data-ready interrupt
    // while awake; also the candidate deep-sleep touch-wake source for a later
    // milestone (M2, pending the sleep-floor measurement).
    static constexpr uint8_t TOUCH_INT_PIN = 2;

private:

    // Sole AXS5106L touchpoint read: true + point (already in panel/LVGL space) on a
    // fresh interrupt, false otherwise. Thin wrapper over the ported driver.
    bool readRawTouch(int16_t& x, int16_t& y);

    void classifyRelease();

    // I2C + reset pins (shared bus)
    static constexpr uint8_t I2C_SDA_PIN  = 4;
    static constexpr uint8_t I2C_SCL_PIN  = 3;
    static constexpr uint8_t TOUCH_RST_PIN = 5;

    // Panel geometry handed to the driver (matches Arduino_GC9309 setRotation(0)); the
    // driver returns coordinates already transformed into this space. If touch axes end
    // up mirrored/rotated vs the display on hardware, adjust PANEL_ROTATION (0..3).
    static constexpr uint16_t PANEL_WIDTH    = 240;
    static constexpr uint16_t PANEL_HEIGHT   = 296;
    static constexpr uint8_t  PANEL_ROTATION = 0;

    // A touch is considered released only after this long with no fresh point (a few
    // report periods) — converts the driver's event stream into a stable held state.
    // Only movement-based gestures (swipe, slider drag) and brief taps are bound, so a
    // rare early release on a perfectly-still long hold is harmless (LONG_PRESS is
    // unbound; a re-touch simply resumes a drag).
    static constexpr uint32_t RELEASE_GRACE_MS = 80;

    // Gesture thresholds (panel pixels / milliseconds)
    static constexpr int16_t  TAP_MAX_MOVE_PX   = 16;   // travel ≤ this → tap, not swipe
    static constexpr int16_t  SWIPE_MIN_MOVE_PX = 28;   // net axis travel ≥ this → swipe
    static constexpr uint32_t LONG_PRESS_MS     = 600;  // held (no travel) ≥ this → long-press

    Axs5106lTouch _driver;
    bool _available = false;           // driver ACKed at begin()

    // Touch state (all owned by the single-threaded poll(); no spinlock needed)
    bool _down = false;
    bool _just_pressed = false;
    bool _just_released = false;
    int16_t _x = 0, _y = 0;             // current point
    int16_t _down_x = 0, _down_y = 0;   // touch-down point
    uint32_t _down_ms = 0;              // touch-down time
    uint32_t _last_point_ms = 0;        // time of the most recent fresh point
    int16_t _max_move = 0;              // peak per-axis travel during the touch
    TouchGesture _gesture = TouchGesture::NONE;

    bool _initialized = false;

};
