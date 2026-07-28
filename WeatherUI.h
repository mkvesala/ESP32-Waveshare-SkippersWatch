#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "IScreenUI.h"
#include "ESPNowReceiver.h"

// === C L A S S  W E A T H E R U I ===
//
// - Weather page adapter: Temp °C / Humid % / Pres hPa (all shown at once)
// - Source: WEATHER_DELTA
// - Realizes IScreenUI over container ui_ContainerWeather
// - Owned by: CrowPanelApplication

class WeatherUI : public IScreenUI {

public:

    explicit WeatherUI(ESPNowReceiver& receiver);

    void begin() override;
    lv_obj_t* getLvglScreen() const override;
    void onEnter() override;
    void update() override;

private:

    ESPNowReceiver& _receiver;
    bool _initialized = false;

    void showWaiting();

    static constexpr uint32_t WEATHER_TIMEOUT_MS = 6000;

};
