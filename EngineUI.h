#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "IScreenUI.h"
#include "ESPNowReceiver.h"

// === C L A S S  E N G I N E U I ===
//
// - Engine room page adapter: Exhaust °C / Fuel L / Water L
// - Sources: HALMET_ENGINE_DELTA (exhaust temp, Kelvin) + HALMET_TANK_DELTA (fuel ratio 0..1)
// - Fuel arrives as a 0..1 ratio → liters via FUEL_TANK_CAPACITY_L (confirm real capacity)
// - Freshwater (Water L) has NO fleet sender yet → permanently shows "---"
// - Realizes IScreenUI over container ui_ContainerEngineroom
// - Owned by: CrowPanelApplication

class EngineUI : public IScreenUI {

public:

    explicit EngineUI(ESPNowReceiver& receiver);

    void begin() override;
    lv_obj_t* getLvglScreen() const override;
    void onEnter() override;
    void update() override;

private:

    ESPNowReceiver& _receiver;
    bool _initialized = false;

    void showWaiting();

    static constexpr uint32_t ENGINE_TIMEOUT_MS = 6000;
    static constexpr uint32_t TANK_TIMEOUT_MS   = 6000;

    // TODO: confirm the real fuel tank capacity for M/V Frida (liters).
    static constexpr float FUEL_TANK_CAPACITY_L = 400.0f;

};
