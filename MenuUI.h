#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "NightPalette.h"

// === C L A S S  M E N U U I ===
//
// - The page menu: SquareLine's ui_ContainerMenu with one image button per page.
//   NOT an IScreenUI page — it is an overlay that never enters the carousel and is
//   never persisted as the last-active page.
// - Driven by WatchApplication::handleTouch: swipe down on any page → show(), swipe up
//   → hide() and the last-active page returns, tap on a button → pageAt() gives the
//   page index to jump to.
// - Touch is dispatched by the app, not by an LVGL input device, so hit-testing is done
//   here from the buttons' LVGL coordinates (tracks the SquareLine layout automatically).
// - Owned by: WatchApplication.

class MenuUI {

public:

    // Hide the container (the export leaves it visible). Call after ui_init().
    void begin();

    void show();
    void hide();
    bool isVisible() const { return _visible; }

    // Night palette: the full-color icons are recolored to a flat red (alpha kept, so
    // the icon shapes stay); off restores the original artwork.
    void setNightMode(bool on);

    // Page index (carousel order) of the button under (x, y), or -1 for none.
    int8_t pageAt(int16_t x, int16_t y) const;

    // Carousel order the buttons map to — must match WatchApplication's addScreen() order.
    enum Page : int8_t {
        PAGE_NAVIGATION = 0,
        PAGE_BATTERIES  = 1,
        PAGE_WEATHER    = 2,
        PAGE_ENGINE     = 3,
        PAGE_ATTITUDE   = 4,
        PAGE_CLOCK      = 5,
        PAGE_SETUP      = 6,
    };

private:

    static constexpr uint32_t NIGHT_ICON = NightPalette::RED_BRIGHT;

    // Finger padding around each 96×96 button for hit-testing
    static constexpr int16_t TOUCH_PAD_PX = 8;

    bool _visible = false;
    bool _initialized = false;

};
