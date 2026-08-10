#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "IScreenUI.h"

// === C L A S S  C L O C K U I ===
//
// - Clock page adapter: local date + weekday + local HH:MM:SS (24h) + UTC HH:MM:SS,
//   and a PREV./NEXT indicator under UTC when the UTC date differs from the local date
// - Reads the ESP32 system clock (POSIX time.h). The clock is disciplined by GNSS UTC:
//   CrowPanelApplication applies incoming DATETIME_DELTA packets via settimeofday() and
//   sets the Finland TZ once. This adapter has NO ESPNowReceiver dependency — it only
//   formats whatever the system clock currently holds.
// - "Never show a fabricated time": until the clock has been set (system year < 2020)
//   all fields show placeholders.
// - Realizes IScreenUI over container ui_ContainerClock
// - Owned by: CrowPanelApplication

class ClockUI : public IScreenUI {

public:

    explicit ClockUI() = default;

    void begin() override;
    lv_obj_t* getLvglScreen() const override;
    void onEnter() override;
    void onLeave() override;
    void update() override;

private:

    bool _initialized = false;

    // Epoch second last rendered — update() rewrites the labels only when the second
    // changes (it is polled ~17 Hz but the display ticks at 1 Hz). -1 forces a redraw.
    long _last_shown_sec = -1;

    void showWaiting();

    // System clock is considered "set" only once GNSS has disciplined it. Before that
    // gettimeofday() returns ~1970, so a year threshold is a robust "have we synced?" test.
    static constexpr int MIN_VALID_YEAR = 2020;

};
