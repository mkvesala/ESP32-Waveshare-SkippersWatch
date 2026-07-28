#pragma once

#include <Arduino.h>
#include <lvgl.h>

// === I N T E R F A C E  I S C R E E N U I ===
//
// - Abstract base class for all UI screen adapters
// - Defines the interface used by ScreenManager and CrowPanelApplication
// - Inherited by: CompassUI, AttitudeUI, BrightnessUI, WeatherUI, ...
//
// Lifecycle (managed by CrowPanelApplication + ScreenManager):
//   begin()              — called by CrowPanelApplication before addScreen()
//   getLvglScreen()      — returns the LVGL lv_obj_t* for this screen
//   onEnter()            — called by ScreenManager when screen becomes active
//   onLeave()            — called by ScreenManager before switching away (cleanup)
//   update()             — called by CrowPanelApplication at UI_UPDATE_INTERVAL_MS
//   onButtonPress()      — called by CrowPanelApplication on knob button press
//   onRotation(dir)      — called by CrowPanelApplication on knob rotation
//                          only if interceptsRotation() returns true
//   interceptsRotation() — return true to consume rotation events instead of
//                          switching screens (e.g. BrightnessUI in ADJUSTING mode)

class IScreenUI {

public:

    virtual ~IScreenUI() = default;

    virtual void begin() = 0;
    virtual lv_obj_t* getLvglScreen() const = 0;

    virtual void onEnter(){}
    virtual void onLeave(){}
    virtual void update(){}
    virtual void onButtonPress(){}
    virtual void onRotation(int8_t dir){}

    virtual bool interceptsRotation() const { return false; }

};
