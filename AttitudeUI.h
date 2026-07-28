#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "IScreenUI.h"
#include "ESPNowReceiver.h"

// === C L A S S  A T T I T U D E U I ===
//
// - Attitude page adapter: Bow up/down (pitch) and Port/Stbd (roll) + session max/min
// - Source: HEADING_DELTA (pitch_rad, roll_rad → x10 integer degrees)
// - Sign convention: pitch > 0 = bow up; roll > 0 = starboard heel
// - Session extremes come from ESPNowReceiver's lifetime min/max pitch/roll API
// - Realizes IScreenUI over container ui_ContainerAttitude
// - Owned by: CrowPanelApplication

class AttitudeUI : public IScreenUI {

public:

    explicit AttitudeUI(ESPNowReceiver& receiver);

    void begin() override;
    lv_obj_t* getLvglScreen() const override;
    void onEnter() override;
    void update() override;

private:

    ESPNowReceiver& _receiver;
    bool _initialized = false;

    void showWaiting();
    void updateMarkers();

    static constexpr uint32_t HEADING_TIMEOUT_MS = 5000;

};
