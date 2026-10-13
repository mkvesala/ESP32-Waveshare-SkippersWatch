#pragma once

#include <Arduino.h>
#include "ft3168_touch.h"
#include "GestureClassifier.h"

// === C L A S S  T O U C H I N P U T ===
//
// - Manages the Waveshare AMOLED 2.06 capacitive touch (FT3168) — the watch's only
//   input device now that there is no knob.
// - Init: _touch.begin() (Wire already begun by the app);  Poll: _touch.poll(millis()).
// - No FreeRTOS task: polling on the main loop avoids I2C concurrency on the bus shared
//   with the AXP2101 PMIC (SDA=GPIO15, SCL=GPIO14).
// - Owned by: WatchApplication.
//
// Gestures are recognized in SOFTWARE from the touch-point stream (chip-agnostic, see
// GestureClassifier). The FT3168 driver reports a level (finger present or not) on each
// poll; a short release grace still debounces a single dropped report so drags and
// swipes don't break up.

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

    // Put the controller into hibernate before deep sleep (it cannot wake the SoC:
    // its INT line, GPIO38, is not RTC-capable). begin() resets it out again on wake.
    void hibernate();

    // Touch reset line, held high through deep sleep by PowerManager so the controller
    // stays in hibernate instead of rebooting into monitor mode.
    static constexpr uint8_t TOUCH_RST_PIN = 9;

private:

    // Sole FT3168 touchpoint read: true + point (panel/LVGL space) while a finger is
    // down, false otherwise. Thin wrapper over the driver.
    bool readRawTouch(int16_t& x, int16_t& y);

    void classifyRelease();

    // Panel geometry (Arduino_CO5300 rotation 0). The FT3168 reports in this space
    // directly [M: confirm on hardware]; points outside are clamped.
    static constexpr uint16_t PANEL_WIDTH  = 410;
    static constexpr uint16_t PANEL_HEIGHT = 502;

    // A touch is considered released only after this long with no fresh point (a few
    // report periods) — rides over a single missed read so a held finger stays held.
    // Only movement-based gestures (swipe, slider drag) and brief taps are bound, so a
    // rare early release on a perfectly-still long hold is harmless (LONG_PRESS is
    // unbound; a re-touch simply resumes a drag).
    static constexpr uint32_t RELEASE_GRACE_MS = 80;

    Ft3168Touch _driver;
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
