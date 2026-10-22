#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include <Preferences.h>
#include "IScreenUI.h"
#include "PanelBrightness.h"
#include "NightPalette.h"

// === C L A S S  S E T U P U I ===
//
// - Setup page adapter, TOUCH-ONLY. Five controls:
//     * ui_SliderBrightness    — drag to set panel brightness
//     * ui_SliderWaketime      — drag to set the wake time (display-on window, 2..20 s);
//                                disabled while prevent sleep is on (no timeout to set)
//     * ui_SwitchPreventSleep  — tap to keep the watch awake on battery
//     * ui_SwitchDemoMode      — tap to show made-up values instead of ESP-NOW data
//     * ui_SwitchNightMode     — tap to draw the whole UI in red
// - All settings persist to NVS (namespace "display")
// - Brightness reaches the AMOLED through PanelBrightness (a panel command, 0..255);
//   there is no backlight LED on this board.
// - The SquareLine value labels (ui_LabelBrightnessValue, ui_LabelPreventSleepValue)
//   are hidden in the export and deliberately never written: the slider position and
//   the switch colour already show the state. ui_LabelWaketime is the exception — it
//   reads "Wake time N s", because whole seconds are hard to judge from a slider alone.
// - This class only STORES the demo and night choices and paints its own widgets; the
//   app reads them every loop and applies them (data source, theme).
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

    // User's wake time in ms, handed to PowerManager every loop (the ladder may shorten it)
    uint32_t waketimeMs() const { return (uint32_t)_waketime_s * 1000UL; }

    bool demoMode() const  { return _demo_mode; }
    bool nightMode() const { return _night_mode; }

    // Touch: the only way this page is operated. interceptsTouchAt() claims a touch-down
    // that lands on a slider or a switch and remembers WHICH; the app then streams
    // onTouchDrag() and finally onTouchEnd(). A tap arrives through the same pair, so the
    // switches need no separate tap path.
    bool interceptsTouchAt(int16_t x, int16_t y) override;
    void onTouchDrag(int16_t x, int16_t y) override;
    void onTouchEnd() override;

private:

    // Which control the in-flight touch was claimed for
    enum class TouchTarget {
        NONE, SLIDER_BRIGHTNESS, SLIDER_WAKETIME, SWITCH_PREVENT, SWITCH_DEMO, SWITCH_NIGHT
    };

    bool _initialized = false;
    TouchTarget _touch_target = TouchTarget::NONE;

    int8_t  _brightness_percent = DEFAULT_BRIGHTNESS_PERCENT;
    int8_t  _ceiling_percent = MAX_BRIGHTNESS_PERCENT;   // power-layer clamp (100 = none)
    uint8_t _waketime_s = DEFAULT_WAKETIME_S;
    bool _prevent_sleep = false;
    bool _demo_mode = false;
    bool _night_mode = false;
    PanelBrightness& _brightness;

    void applyBrightness();
    // What actually reaches the panel: the user's brightness, clamped by the ceiling
    int8_t effectivePercent() const;
    void updateUI();
    uint8_t percentToLevel(int8_t percent) const;

    // Touch x across a slider's box → a value in that slider's own range (the range comes
    // from the SquareLine export, e.g. 2..20 for the wake time)
    static int32_t sliderValueAt(lv_obj_t* slider, int16_t x);
    static bool hitsSwitch(lv_obj_t* sw, int16_t x, int16_t y);
    static bool hitsSlider(lv_obj_t* slider, int16_t x, int16_t y);

    bool waketimeEnabled() const { return !_prevent_sleep; }

    // Paint every widget this page owns from the current state and palette: slider
    // tracks/indicators/knobs (rest, dragging, disabled) and the three switches.
    void applyVisuals();
    void styleSlider(lv_obj_t* slider, bool active, bool enabled);
    void styleSwitch(lv_obj_t* sw, bool checked);
    uint32_t color(uint32_t day_rgb) const { return NightPalette::pick(day_rgb, _night_mode); }

    // NVS helpers — one namespace, one key per setting
    static int8_t  loadChar(const char* key, int8_t def);
    static void    saveChar(const char* key, int8_t v);
    static bool    loadBool(const char* key, bool def);
    static void    saveBool(const char* key, bool v);

    // Slider indicator + knob colors, code-owned (these override the SquareLine theme):
    //  - SLIDER_TRACK : the slider's background (as exported by SquareLine)
    //  - SLIDER_BASE  : resting look
    //  - ADJUST_ACCENT: while a drag is in flight, so the interaction is unmistakable
    //  - SLIDER_OFF   : a disabled slider (wake time while prevent sleep is on)
    static constexpr uint32_t SLIDER_TRACK  = 0x666666;
    static constexpr uint32_t SLIDER_BASE   = 0xCCCCCC;  // almost white grey (resting)
    static constexpr uint32_t ADJUST_ACCENT = 0xFF9800;  // amber (dragging)
    static constexpr uint32_t SLIDER_OFF    = 0x444444;  // dark grey (disabled)

    // Switch colors, shared by all three switches. Knob carries the bright hue, the track
    // the darker shade of the same hue. Red/green match the battery bar buckets in
    // BatteryMonitor, so the whole UI shares one palette. Night mode has its own explicit
    // table: off/on must stay distinguishable when hue is gone, so it is done by brightness.
    static constexpr uint32_t SWITCH_OFF_KNOB  = 0xF44336;  // red
    static constexpr uint32_t SWITCH_OFF_TRACK = 0x6E1F19;  // dark red
    static constexpr uint32_t SWITCH_ON_KNOB   = 0x4CAF50;  // green
    static constexpr uint32_t SWITCH_ON_TRACK  = 0x24571F;  // dark green
    static constexpr uint32_t NIGHT_OFF_KNOB   = NightPalette::RED_DARK;
    static constexpr uint32_t NIGHT_OFF_TRACK  = NightPalette::RED_DIM;
    static constexpr uint32_t NIGHT_ON_KNOB    = NightPalette::RED_BRIGHT;
    static constexpr uint32_t NIGHT_ON_TRACK   = NightPalette::RED_MID;

    // Finger padding around a control's box for touch hit-testing (the sliders are only
    // ~20 px tall and the switches 25 px — this makes all of them comfortably tappable).
    static constexpr int16_t TOUCH_PAD_PX = 18;

    static constexpr int8_t MIN_BRIGHTNESS_PERCENT = 2;
    static constexpr int8_t MAX_BRIGHTNESS_PERCENT = 100;

    // Wake time bounds. The slider's own range (SquareLine) is 2..20 as well; these clamp
    // whatever comes out of NVS, so a corrupt value can never mean "never sleep".
    static constexpr uint8_t MIN_WAKETIME_S     = 2;
    static constexpr uint8_t MAX_WAKETIME_S     = 20;
    static constexpr uint8_t DEFAULT_WAKETIME_S = 10;   // = the pre-slider fixed timeout

    static constexpr const char* NVS_NAMESPACE = "display";
    static constexpr const char* NVS_KEY_BRIGHTNESS = "brightness";
    static constexpr const char* NVS_KEY_PREVENT_SLEEP = "nosleep";
    static constexpr const char* NVS_KEY_WAKETIME = "waketime";
    static constexpr const char* NVS_KEY_DEMO = "demo";
    static constexpr const char* NVS_KEY_NIGHT = "night";
    static constexpr int8_t DEFAULT_BRIGHTNESS_PERCENT = 20;  // ~51/255 — readable in
                                                             // daylight, low continuous draw
                                                             // (AMOLED draw vs level: [M]).
                                                             // Fallback only: NVS-stored value wins.

};
