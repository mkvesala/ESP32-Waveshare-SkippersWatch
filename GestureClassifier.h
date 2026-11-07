#pragma once

#include <stdint.h>

// === G E S T U R E  C L A S S I F I E R ===
//
// - Pure gesture classification of one finished touch: no Arduino, no I2C, no state.
//   TouchInput feeds it the touch-down point, the last point, the peak per-axis travel
//   and the held time; host unit tests (tests/host) feed it synthetic touches.
// - Panel y grows DOWNWARD, so a finger swiping UP produces a NEGATIVE net dy.
// - Little peak travel → TAP (or LONG_PRESS by duration). Otherwise the dominant axis of
//   the NET travel decides, and it must reach SWIPE_MIN_MOVE_PX; anything else is NONE.
//   Ties go to the vertical axis (menu pull-down wins over a page step).
// - Thresholds are the CrowPanel values scaled by the resolution ratio (≈1.7×,
//   240×296 → 410×502), so the gestures feel the same physical size.

enum class TouchGesture : uint8_t {
    NONE = 0,
    TAP,          // brief contact, little travel → page's onButtonPress() / menu button
    SWIPE_UP,     // finger up    → hide the page menu
    SWIPE_DOWN,   // finger down  → show the page menu
    SWIPE_LEFT,   // finger left  → next page
    SWIPE_RIGHT,  // finger right → previous page
    LONG_PRESS,   // held with little travel — recognized but currently unbound (reserved)
};

struct GestureClassifier {

    static constexpr int16_t  TAP_MAX_MOVE_PX   = 27;   // peak travel ≤ this → tap
    static constexpr int16_t  SWIPE_MIN_MOVE_PX = 48;   // net axis travel ≥ this → swipe
    static constexpr uint32_t LONG_PRESS_MS     = 600;  // held (no travel) ≥ this → long-press

    // Double tap: a second TAP soon after the first and near it. Both taps are still
    // delivered as TAPs, so single-tap pages see no delay.
    static constexpr uint32_t DOUBLE_TAP_MS       = 400;  // between the two releases
    static constexpr int16_t  DOUBLE_TAP_MAX_PX   = 60;   // per axis between the two taps

    static bool isDoubleTap(uint32_t first_ms, int16_t first_x, int16_t first_y,
                            uint32_t second_ms, int16_t second_x, int16_t second_y) {
        if ((uint32_t)(second_ms - first_ms) > DOUBLE_TAP_MS) return false;
        const int32_t dx = (int32_t)second_x - first_x;
        const int32_t dy = (int32_t)second_y - first_y;
        return (dx < 0 ? -dx : dx) <= DOUBLE_TAP_MAX_PX && (dy < 0 ? -dy : dy) <= DOUBLE_TAP_MAX_PX;
    }

    static TouchGesture classify(int16_t down_x, int16_t down_y,
                                 int16_t up_x, int16_t up_y,
                                 int16_t max_move, uint32_t held_ms) {
        if (max_move <= TAP_MAX_MOVE_PX) {
            return (held_ms >= LONG_PRESS_MS) ? TouchGesture::LONG_PRESS : TouchGesture::TAP;
        }

        const int32_t dx  = (int32_t)up_x - down_x;
        const int32_t dy  = (int32_t)up_y - down_y;
        const int32_t adx = (dx < 0) ? -dx : dx;
        const int32_t ady = (dy < 0) ? -dy : dy;

        if (ady >= adx) {
            if (ady < SWIPE_MIN_MOVE_PX) return TouchGesture::NONE;
            return (dy < 0) ? TouchGesture::SWIPE_UP : TouchGesture::SWIPE_DOWN;
        }
        if (adx < SWIPE_MIN_MOVE_PX) return TouchGesture::NONE;
        return (dx < 0) ? TouchGesture::SWIPE_LEFT : TouchGesture::SWIPE_RIGHT;
    }

};
