#pragma once

#include <stdint.h>

// === N I G H T  P A L E T T E ===
//
// - Night mode keeps the eye dark-adapted: everything on the panel is drawn in shades
//   of red only. Pure (no Arduino/LVGL), so host unit tests can exercise it.
// - map(): a day color → the red of the same perceived brightness (R = luma, G = B = 0).
//   Near-black (below NIGHT_BLACK_BELOW, e.g. the theme's 0x15171A screen) becomes true
//   black, so the background never glows red; any other color is lifted to at least
//   NIGHT_MIN_RED so a dark grey track does not vanish completely.
// - pick(): the one call every code-owned color goes through, so the day and the night
//   palettes can never drift apart structurally.
// - Colors whose meaning is their HUE (the battery-bar buckets) cannot be mapped by
//   luma — green and blue come out nearly the same red. Those owners keep an explicit
//   night table instead (BatteryMonitor).

struct NightPalette {

    static constexpr uint8_t  NIGHT_BLACK_BELOW = 0x20;
    static constexpr uint8_t  NIGHT_MIN_RED     = 0x30;

    // Named night shades for explicit tables (switches, battery buckets)
    static constexpr uint32_t RED_BRIGHT = 0xFF0000;
    static constexpr uint32_t RED_MID    = 0xB00000;
    static constexpr uint32_t RED_DARK   = 0x700000;
    static constexpr uint32_t RED_DIM    = 0x300000;

    static constexpr uint32_t map(uint32_t rgb) {
        return luma(rgb) < NIGHT_BLACK_BELOW ? 0 : (uint32_t)lift(luma(rgb)) << 16;
    }

    static constexpr uint32_t pick(uint32_t day_rgb, bool night) {
        return night ? map(day_rgb) : day_rgb;
    }

    // Rec. 601 luma in 8-bit fixed point (weights 77/150/29 sum to 256)
    static constexpr uint8_t luma(uint32_t rgb) {
        return (uint8_t)((((rgb >> 16) & 0xFF) * 77 +
                          ((rgb >>  8) & 0xFF) * 150 +
                          ( rgb        & 0xFF) * 29) >> 8);
    }

private:

    static constexpr uint8_t lift(uint8_t v) {
        return v < NIGHT_MIN_RED ? NIGHT_MIN_RED : v;
    }

};
