#include "SetupUI.h"
#include "ui.h"

// === P U B L I C ===

SetupUI::SetupUI(PanelBrightness& brightness)
    : _brightness(brightness) {}

lv_obj_t* SetupUI::getLvglScreen() const {
    return ui_ContainerSetup;
}

void SetupUI::begin() {
    if (_initialized) return;
    _brightness_percent = this->loadBrightness();
    _prevent_sleep = this->loadPreventSleep();
    // Do NOT light the backlight here — the panel stays dark through init/first
    // render; the app calls rampBacklightUp() afterwards (staggered wake).
    this->styleSwitch();
    this->updateUI();
    this->setSliderActive(false);
    this->applyPreventSleepVisual();
    _initialized = true;
}

// Soft-ramp brightness 0 → stored brightness (soft-start, never a hard step).
// Ramps to the CLAMPED target, so a low-battery wake never flashes bright then dims.
void SetupUI::rampBacklightUp() {
    uint8_t target = this->percentToLevel(this->effectivePercent());
    uint8_t step = (target / 16) > 0 ? (target / 16) : 1;
    for (uint16_t d = 0; d < target; d += step) {
        _brightness.set((uint8_t)d);
        delay(8);
    }
    _brightness.set(target);
}

// Apply a new clamp. Called every loop by the app, so it must be cheap: no-op unless the
// ceiling actually moved. A ceiling change is a load DECREASE (or a few-mA increase back
// to the user's setting), so a direct write is fine — same as the drag path.
void SetupUI::setBrightnessCeiling(int8_t percent) {
    if (percent < MIN_BRIGHTNESS_PERCENT) percent = MIN_BRIGHTNESS_PERCENT;
    if (percent > MAX_BRIGHTNESS_PERCENT) percent = MAX_BRIGHTNESS_PERCENT;
    if (percent == _ceiling_percent) return;
    _ceiling_percent = percent;
    if (_initialized) this->applyBrightness();
}

// Claim a touch-down that lands on either control (with a little finger padding), so the
// app routes it here instead of treating it as a swipe. Remembers WHICH control was hit —
// the drag/end pair that follows carries no coordinates for the switch case. The boxes
// come from LVGL, so this tracks the SquareLine layout automatically.
bool SetupUI::interceptsTouchAt(int16_t x, int16_t y) {
    _touch_target = TouchTarget::NONE;
    if (!_initialized) return false;

    lv_area_t a;

    lv_obj_get_coords(ui_SwitchPreventSleep, &a);
    if (x >= (a.x1 - TOUCH_PAD_PX) && x <= (a.x2 + TOUCH_PAD_PX) &&
        y >= (a.y1 - TOUCH_PAD_PX) && y <= (a.y2 + TOUCH_PAD_PX)) {
        _touch_target = TouchTarget::SWITCH;
        return true;
    }

    lv_obj_get_coords(ui_SliderBrightness, &a);
    if (x >= a.x1 && x <= a.x2 &&
        y >= (a.y1 - TOUCH_PAD_PX) && y <= (a.y2 + TOUCH_PAD_PX)) {
        _touch_target = TouchTarget::SLIDER;
        return true;
    }

    return false;
}

// Live slider drag: map the touch x across the slider's width to a percentage and apply
// immediately (panel brightness + slider position). Touch is absolute, so no capture mode is needed.
// The switch is a discrete control — it acts on release, not while the finger moves.
void SetupUI::onTouchDrag(int16_t x, int16_t y) {
    if (!_initialized || _touch_target != TouchTarget::SLIDER) return;

    lv_area_t a;
    lv_obj_get_coords(ui_SliderBrightness, &a);
    const int32_t span = (int32_t)a.x2 - (int32_t)a.x1;
    if (span <= 0) return;
    int32_t clamped_x = x;
    if (clamped_x < a.x1) clamped_x = a.x1;
    if (clamped_x > a.x2) clamped_x = a.x2;
    // Slider uses LVGL's default 0..100 range, which maps 1:1 to brightness percent.
    int32_t pct = ((clamped_x - a.x1) * 100 + span / 2) / span;
    if (pct < MIN_BRIGHTNESS_PERCENT) pct = MIN_BRIGHTNESS_PERCENT;
    if (pct > MAX_BRIGHTNESS_PERCENT) pct = MAX_BRIGHTNESS_PERCENT;
    _brightness_percent = (int8_t)pct;
    this->setSliderActive(true);   // visual feedback while the finger is down
    this->applyBrightness();
    this->updateUI();
}

// Claimed touch released. Slider: persist the brightness once, off the hot path. Switch:
// flip the choice, repaint and persist immediately — it is a single deliberate gesture,
// and onLeave() never runs on the way into deep sleep, so a later save would lose it.
void SetupUI::onTouchEnd() {
    if (!_initialized) return;

    switch (_touch_target) {
        case TouchTarget::SLIDER:
            this->saveBrightness();
            this->setSliderActive(false);
            break;

        case TouchTarget::SWITCH:
            _prevent_sleep = !_prevent_sleep;
            this->applyPreventSleepVisual();
            this->savePreventSleep();
            Serial.printf("[setup] prevent sleep %s\n", _prevent_sleep ? "ON" : "OFF");
            break;

        default:
            break;
    }
    _touch_target = TouchTarget::NONE;
}

// === P R I V A T E ===

// Slider indicator + knob: amber while a drag is in flight, base grey at rest.
void SetupUI::setSliderActive(bool active) {
    lv_color_t c = lv_color_hex(active ? ADJUST_ACCENT : SLIDER_BASE);
    lv_obj_set_style_bg_color(ui_SliderBrightness, c, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ui_SliderBrightness, c, LV_PART_KNOB);
}

// One-time switch styling. The SquareLine export ships no styles for this widget, so the
// look is entirely code-owned here (same convention as the slider colors above).
// LVGL draws LV_PART_MAIN as the track when unchecked and LV_PART_INDICATOR over it when
// checked, so both parts get a color for both states — otherwise the off-red would show
// at the edges of the on-green track.
void SetupUI::styleSwitch() {
    lv_obj_set_style_bg_color(ui_SwitchPreventSleep, lv_color_hex(SWITCH_OFF_TRACK),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_SwitchPreventSleep, lv_color_hex(SWITCH_OFF_KNOB),
                              LV_PART_KNOB | LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(ui_SwitchPreventSleep, lv_color_hex(SWITCH_ON_TRACK),
                              LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(ui_SwitchPreventSleep, lv_color_hex(SWITCH_ON_TRACK),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(ui_SwitchPreventSleep, lv_color_hex(SWITCH_ON_KNOB),
                              LV_PART_KNOB | LV_STATE_CHECKED);

    lv_obj_set_style_bg_opa(ui_SwitchPreventSleep, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_SwitchPreventSleep, 255, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(ui_SwitchPreventSleep, 255, LV_PART_KNOB | LV_STATE_DEFAULT);
}

// Drive the widget's CHECKED state from our own flag. The switch is not wired to an LVGL
// input device — touch is dispatched by the app — so this is the only thing that moves it.
void SetupUI::applyPreventSleepVisual() {
    if (_prevent_sleep) {
        lv_obj_add_state(ui_SwitchPreventSleep, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(ui_SwitchPreventSleep, LV_STATE_CHECKED);
    }
}

void SetupUI::applyBrightness() {
    _brightness.set(this->percentToLevel(this->effectivePercent()));
}

int8_t SetupUI::effectivePercent() const {
    return (_brightness_percent < _ceiling_percent) ? _brightness_percent : _ceiling_percent;
}

// Only the slider position is refreshed: ui_LabelBrightnessValue is hidden in the export
// and intentionally left alone (the slider already shows the level).
void SetupUI::updateUI() {
    lv_slider_set_value(ui_SliderBrightness, _brightness_percent, LV_ANIM_OFF);
}

void SetupUI::saveBrightness() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putChar(NVS_KEY_BRIGHTNESS, _brightness_percent);
    prefs.end();
}

int8_t SetupUI::loadBrightness() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    int8_t v = prefs.getChar(NVS_KEY_BRIGHTNESS, DEFAULT_BRIGHTNESS_PERCENT);
    prefs.end();
    if (v < MIN_BRIGHTNESS_PERCENT) v = MIN_BRIGHTNESS_PERCENT;
    if (v > MAX_BRIGHTNESS_PERCENT) v = MAX_BRIGHTNESS_PERCENT;
    return v;
}

void SetupUI::savePreventSleep() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBool(NVS_KEY_PREVENT_SLEEP, _prevent_sleep);
    prefs.end();
}

// Defaults to false: the endurance-preserving behaviour is the one you get if nothing
// was ever chosen, and a prevent-sleep flag should never appear out of nowhere.
bool SetupUI::loadPreventSleep() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    bool v = prefs.getBool(NVS_KEY_PREVENT_SLEEP, false);
    prefs.end();
    return v;
}

uint8_t SetupUI::percentToLevel(int8_t percent) const {
    return (uint8_t)((int)percent * 255 / 100);
}
