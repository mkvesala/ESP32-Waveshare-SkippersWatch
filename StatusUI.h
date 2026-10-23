#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "SystemStats.h"
#include "BatteryMonitor.h"

// === C L A S S  S T A T U S U I ===
//
// - The Status screen: SquareLine's ui_ContainerStatus with a titles column
//   (ui_LabelInfo) and a values column (ui_LabelInfo1). NOT an IScreenUI page — an
//   overlay like MenuUI: swipe up on any page opens it, swipe down returns to that
//   page; never part of the carousel, never the last-active page.
// - The titles are written from code in begin(), from the same row list the values are
//   built from, so the two columns can never get out of step (the SquareLine text is
//   only a layout hint).
// - update() refreshes the values once per second while the overlay is visible.
// - Owned by: WatchApplication.

class StatusUI {

public:

    StatusUI(const SystemStats& stats, const BatteryMonitor& battery)
        : _stats(stats), _battery(battery) {}

    // Hide the container (the export leaves it visible) and write the titles
    void begin();

    void show();
    void hide();
    bool isVisible() const { return _visible; }

    void update(uint32_t now);

private:

    static constexpr uint32_t REFRESH_MS = 1000;

    const SystemStats& _stats;
    const BatteryMonitor& _battery;
    bool _visible = false;
    bool _initialized = false;
    uint32_t _last_refresh_ms = 0;

    void render();

};
