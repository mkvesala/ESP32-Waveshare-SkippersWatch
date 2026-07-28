#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include <Preferences.h>
#include "IScreenUI.h"

// === C L A S S  S E T U P U I ===
//
// - Setup page adapter: backlight brightness via ui_SliderBrightness + label
// - Knob press → ADJUSTING (rotation intercepted to change brightness)
// - Knob rotation → ±BRIGHTNESS_STEP %, applied to LEDC backlight immediately
// - 3 s idle or page leave → save brightness to NVS ("display"/"brightness"), back to IDLE
// - Mirrors the verrokki BrightnessUI, adapted from an arc to the watch's slider
// - Realizes IScreenUI over container ui_ContainerSetup
// - Owned by: CrowPanelApplication

class SetupUI : public IScreenUI {

public:

    explicit SetupUI(uint8_t backlight_pin);

    void begin() override;
    lv_obj_t* getLvglScreen() const override;
    void update() override;

    // Soft-ramp the backlight from 0 to the stored brightness — called by the app
    // AFTER the first frame is rendered into the dark panel (staggered wake). Keeps
    // the backlight rise as its own load step, never simultaneous with radio bring-up.
    void rampBacklightUp();
    void onButtonPress() override;
    void onRotation(int8_t dir) override;
    bool interceptsRotation() const override;
    void onLeave() override;

private:

    enum class SetupState { IDLE, ADJUSTING };

    SetupState _state = SetupState::IDLE;
    bool _initialized = false;

    int8_t _brightness_percent = DEFAULT_BRIGHTNESS_PERCENT;
    uint8_t _backlight_pin;
    uint32_t _last_rotation_time = 0;

    void applyBrightness();
    void updateUI();
    void saveBrightness();
    int8_t loadBrightness();
    uint8_t percentToPwm(int8_t percent) const;

    static constexpr uint32_t AUTOSAVE_TIMEOUT_MS = 3000;
    static constexpr int8_t BRIGHTNESS_STEP = 2;
    static constexpr int8_t MIN_BRIGHTNESS_PERCENT = 2;
    static constexpr int8_t MAX_BRIGHTNESS_PERCENT = 100;

    static constexpr const char* NVS_NAMESPACE = "display";
    static constexpr const char* NVS_KEY_BRIGHTNESS = "brightness";
    static constexpr int8_t DEFAULT_BRIGHTNESS_PERCENT = 30;  // ~76/255 PWM — readable in
                                                             // daylight, low continuous draw
                                                             // (backlight = biggest load)

};
