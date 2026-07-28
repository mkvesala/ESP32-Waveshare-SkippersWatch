#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "IScreenUI.h"
#include "ESPNowReceiver.h"

// === C L A S S  N A V I G A T I O N U I ===
//
// - Navigation page adapter: COG / HDG / SOG / DPT / POS
// - Sources: GNSS_DELTA (COG, SOG, position) + HEADING_DELTA (HDG) + DEPTH_DELTA (DPT)
// - DPT shows depth below surface (below_surface_m). Two-fold freshness: the ESP-NOW
//   gateway RX timestamp AND the payload's own age_ms (relayed feed liveness); either
//   stale, or a NAN value, → "---" (never a frozen depth under the keel)
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

    // Depth is relayed → gate on both the ESP-NOW gateway liveness (RX timestamp) and the
    // underlying N2K sounder feed liveness (DepthDelta.age_ms). A sounder updates ~1 Hz, so
    // a feed silent >5 s means the bottom was lost or the N2K chain is down.
    static constexpr uint32_t DEPTH_TIMEOUT_MS      = 6000;   // ESP-NOW gateway broadcast timeout
    static constexpr uint32_t DEPTH_FEED_MAX_AGE_MS = 5000;   // in-payload age of the depth feed

};
