#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include <Preferences.h>
#include "IScreenUI.h"
#include "PanelBrightness.h"

// === C L A S S  S E T U P U I ===
//
// - Setup page adapter, TOUCH-ONLY. Two controls:
//     * ui_SliderBrightness    — drag to set backlight brightness
//     * ui_SwitchPreventSleep  — tap to keep the watch awake on battery
// - Both settings persist to NVS ("display"/"brightness", "display"/"nosleep")
// - Brightness reaches the AMOLED through PanelBrightness (a panel command, 0..255);
//   there is no backlight LED on this board.
// - The SquareLine value labels (ui_LabelBrightnessValue, ui_LabelPreventSleepValue)
//   are hidden in the export and deliberately never written: the slider position and
//   the switch colour already show the state, and the numbers only crowded the layout.
// - Realizes IScreenUI over container ui_ContainerSetup
// - Owned by: WatchApplication

class SetupUI : public IScreenUI {

public:

    explicit SetupUI(PanelBrightness& brightness);

    void begin() override;
    lv_obj_t* getLvglScreen() const override;

    // Soft-ramp the panel brightness from 0 to the stored brightness — called by the app
    // AFTER the first frame is rendered into the dark panel (staggered wake). Keeps
    // the backlight rise as its own load step, never simultaneous with radio bring-up.
    void rampBacklightUp();

    // Cap the APPLIED backlight without touching the user's stored setting. Used by the
    // power layer for the low-charge ladder (CRITICAL/CUTOFF) and the stay-awake idle dim.
    // The slider and NVS keep showing/holding what the user chose — only what reaches
    // the panel is clamped, so lifting the ceiling restores their brightness exactly.
    void setBrightnessCeiling(int8_t percent);

    // User's "keep the watch awake on battery" choice. Read every loop by the app and
    // handed to PowerManager, which decides whether it may actually be honoured (the
    // low-charge ladder overrides it from CRITICAL down).
    bool preventSleep() const { return _prevent_sleep; }

    // Touch: the only way this page is operated. interceptsTouchAt() claims a touch-down
    // that lands on the slider or the switch and remembers WHICH; the app then streams
    // onTouchDrag() and finally onTouchEnd(). A tap arrives through the same pair, so the
    // switch needs no separate tap path.
    bool interceptsTouchAt(int16_t x, int16_t y) override;
    void onTouchDrag(int16_t x, int16_t y) override;
    void onTouchEnd() override;

private:

    // Which control the in-flight touch was claimed for
    enum class TouchTarget { NONE, SLIDER, SWITCH };

    bool _initialized = false;
    TouchTarget _touch_target = TouchTarget::NONE;

    int8_t _brightness_percent = DEFAULT_BRIGHTNESS_PERCENT;
    int8_t _ceiling_percent = MAX_BRIGHTNESS_PERCENT;   // power-layer clamp (100 = none)
    bool _prevent_sleep = false;
    PanelBrightness& _brightness;

    void applyBrightness();
    // What actually reaches the panel: the user's brightness, clamped by the ceiling
    int8_t effectivePercent() const;
    void updateUI();
    uint8_t percentToLevel(int8_t percent) const;

    void saveBrightness();
    int8_t loadBrightness();
    void savePreventSleep();
    bool loadPreventSleep();

    // Highlight the slider while a drag is in flight, restore it on release
    void setSliderActive(bool active);
    // Paint the switch and sync LV_STATE_CHECKED to _prevent_sleep
    void applyPreventSleepVisual();
    // One-time switch styling (the SquareLine export ships none for this widget)
    void styleSwitch();

    // Slider indicator + knob colors, code-owned (these override the SquareLine theme):
    //  - SLIDER_BASE  : resting look
    //  - ADJUST_ACCENT: while a drag is in flight, so the interaction is unmistakable
    static constexpr uint32_t SLIDER_BASE   = 0xCCCCCC;  // almost white grey (resting)
    static constexpr uint32_t ADJUST_ACCENT = 0xFF9800;  // amber (dragging)

    // Prevent-sleep switch colors. Knob carries the bright hue, the track the darker
    // shade of the same hue. Red/green match the battery bar buckets in BatteryMonitor,
    // so the whole UI shares one palette.
    static constexpr uint32_t SWITCH_OFF_KNOB  = 0xF44336;  // red
    static constexpr uint32_t SWITCH_OFF_TRACK = 0x6E1F19;  // dark red
    static constexpr uint32_t SWITCH_ON_KNOB   = 0x4CAF50;  // green
    static constexpr uint32_t SWITCH_ON_TRACK  = 0x24571F;  // dark green

    // Finger padding around a control's box for touch hit-testing (the slider is only
    // ~20 px tall and the switch 25 px — this makes both comfortably tappable).
    static constexpr int16_t TOUCH_PAD_PX = 18;

    static constexpr int8_t MIN_BRIGHTNESS_PERCENT = 2;
    static constexpr int8_t MAX_BRIGHTNESS_PERCENT = 100;

    static constexpr const char* NVS_NAMESPACE = "display";
    static constexpr const char* NVS_KEY_BRIGHTNESS = "brightness";
    static constexpr const char* NVS_KEY_PREVENT_SLEEP = "nosleep";
    static constexpr int8_t DEFAULT_BRIGHTNESS_PERCENT = 20;  // ~51/255 — readable in
                                                             // daylight, low continuous draw
                                                             // (AMOLED draw vs level: [M]).
                                                             // Fallback only: NVS-stored value wins.

};
