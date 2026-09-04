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

    // Cap the APPLIED backlight without touching the user's stored setting. Used by the
    // power layer for the low-charge ladder (CRITICAL/CUTOFF) and the USB idle-dim.
    // The slider and NVS keep showing/holding what the user chose — only what reaches
    // the LEDC pin is clamped, so lifting the ceiling restores their brightness exactly.
    void setBrightnessCeiling(int8_t percent);

    void onButtonPress() override;
    void onRotation(int8_t dir) override;
    bool interceptsRotation() const override;
    void onLeave() override;

    // Touch: drag the brightness slider directly. Touch is absolute, so this needs
    // no ADJUSTING mode — it bypasses the encoder's state machine (which stays intact
    // for the knob). interceptsTouchAt() claims a touch-down that lands on the slider;
    // the app then streams onTouchDrag() and finally onTouchEnd() (which persists).
    bool interceptsTouchAt(int16_t x, int16_t y) const override;
    void onTouchDrag(int16_t x, int16_t y) override;
    void onTouchEnd() override;

private:

    enum class SetupState { IDLE, ADJUSTING };

    SetupState _state = SetupState::IDLE;
    bool _initialized = false;

    int8_t _brightness_percent = DEFAULT_BRIGHTNESS_PERCENT;
    int8_t _ceiling_percent = MAX_BRIGHTNESS_PERCENT;   // power-layer clamp (100 = none)
    uint8_t _backlight_pin;
    uint32_t _last_rotation_time = 0;

    void applyBrightness();
    // What actually reaches the panel: the user's brightness, clamped by the ceiling
    int8_t effectivePercent() const;
    void updateUI();
    void saveBrightness();
    int8_t loadBrightness();
    uint8_t percentToPwm(int8_t percent) const;

    // Single point of state change: flips _state AND drives the adjust-mode visual,
    // so the two can never disagree (mirrors the verrokki BrightnessUI::setState)
    void setState(SetupState new_state);
    // ADJUSTING → recolor slider indicator + knob; IDLE → restore the export's theme
    void setAdjustVisual(bool active);

    // Slider indicator + knob colors, code-owned (these override the SquareLine theme):
    //  - SLIDER_BASE  : resting / IDLE look
    //  - ADJUST_ACCENT: ADJUSTING, so it is unmistakable the knob now edits brightness
    static constexpr uint32_t SLIDER_BASE   = 0xCCCCCC;  // almost white grey (resting)
    static constexpr uint32_t ADJUST_ACCENT = 0xFF9800;  // amber (adjusting)

    // Vertical finger padding around the slider's box for touch hit-testing (the
    // slider is only ~20 px tall — this makes it comfortably tappable/draggable).
    static constexpr int16_t TOUCH_SLIDER_PAD_PX = 18;

    static constexpr uint32_t AUTOSAVE_TIMEOUT_MS = 3000;
    static constexpr int8_t BRIGHTNESS_STEP = 2;
    static constexpr int8_t MIN_BRIGHTNESS_PERCENT = 2;
    static constexpr int8_t MAX_BRIGHTNESS_PERCENT = 100;

    static constexpr const char* NVS_NAMESPACE = "display";
    static constexpr const char* NVS_KEY_BRIGHTNESS = "brightness";
    static constexpr int8_t DEFAULT_BRIGHTNESS_PERCENT = 20;  // ~51/255 PWM — readable in
                                                             // daylight, low continuous draw
                                                             // (backlight = biggest load).
                                                             // Fallback only: NVS-stored value wins.

};
