#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "IScreenUI.h"
#include "ESPNowReceiver.h"

// === C L A S S  N A V I G A T I O N U I ===
//
// - Navigation page adapter: COG / HDG / SOG / DPT / POS
// - Sources: GNSS_DELTA (COG, SOG, position) + HEADING_DELTA (HDG)
// - DPT (depth) has NO fleet sender yet → permanently shows "---"
// - Realizes IScreenUI over container ui_ContainerNavigation
// - Owned by: CrowPanelApplication

class NavigationUI : public IScreenUI {

public:

    explicit NavigationUI(ESPNowReceiver& receiver);

    void begin() override;
    lv_obj_t* getLvglScreen() const override;
    void onEnter() override;
    void update() override;

private:

    ESPNowReceiver& _receiver;
    bool _initialized = false;

    void showWaiting();
    void formatPosition(float lat_deg, float lon_deg, char* out, size_t out_len);

    static constexpr uint32_t HEADING_TIMEOUT_MS = 5000;
    static constexpr uint32_t GNSS_TIMEOUT_MS    = 6000;

};
