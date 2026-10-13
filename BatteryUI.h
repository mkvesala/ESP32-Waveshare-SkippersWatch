#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "IScreenUI.h"
#include "ESPNowReceiver.h"

// === C L A S S  B A T T E R Y U I ===
//
// - Batteries page adapter: House V / Start V / House % / House A
// - Source: BATTERY_DELTA (house V/A/SoC + start V; current signed, - = charging)
// - Realizes IScreenUI over container ui_ContainerBatteries
// - Owned by: WatchApplication

class BatteryUI : public IScreenUI {

public:

    explicit BatteryUI(ESPNowReceiver& receiver);

    void begin() override;
    lv_obj_t* getLvglScreen() const override;
    void onEnter() override;
    void update() override;

private:

    ESPNowReceiver& _receiver;
    bool _initialized = false;

    void showWaiting();

    static constexpr uint32_t BATTERY_TIMEOUT_MS = 6000;

};
