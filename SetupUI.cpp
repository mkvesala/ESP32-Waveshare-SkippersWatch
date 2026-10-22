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

    int8_t b = loadChar(NVS_KEY_BRIGHTNESS, DEFAULT_BRIGHTNESS_PERCENT);
    if (b < MIN_BRIGHTNESS_PERCENT) b = MIN_BRIGHTNESS_PERCENT;
    if (b > MAX_BRIGHTNESS_PERCENT) b = MAX_BRIGHTNESS_PERCENT;
    _brightness_percent = b;

    int8_t w = loadChar(NVS_KEY_WAKETIME, DEFAULT_WAKETIME_S);
    if (w < (int8_t)MIN_WAKETIME_S) w = MIN_WAKETIME_S;
    if (w > (int8_t)MAX_WAKETIME_S) w = MAX_WAKETIME_S;
    _waketime_s = (uint8_t)w;

    // Every switch defaults to off: the endurance-preserving, real-data, day-palette
    // behaviour is what you get if nothing was ever chosen.
    _prevent_sleep = loadBool(NVS_KEY_PREVENT_SLEEP, false);
    _demo_mode     = loadBool(NVS_KEY_DEMO, false);
    _night_mode    = loadBool(NVS_KEY_NIGHT, false);

    // Do NOT light the backlight here — the panel stays dark through init/first
    // render; the app calls rampBacklightUp() afterwards (staggered wake).
    this->updateUI();
    this->applyVisuals();
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

// Claim a touch-down that lands on a control (with a little finger padding), so the app
// routes it here instead of treating it as a swipe. Remembers WHICH control was hit —
// the drag/end pair that follows carries no coordinates for the switch case. Switches
// are tested first: their padded boxes are small and must win over a nearby slider. The
// boxes come from LVGL, so this tracks the SquareLine layout automatically.
bool SetupUI::interceptsTouchAt(int16_t x, int16_t y) {
    _touch_target = TouchTarget::NONE;
    if (!_initialized) return false;

    if      (hitsSwitch(ui_SwitchPreventSleep, x, y)) _touch_target = TouchTarget::SWITCH_PREVENT;
    else if (hitsSwitch(ui_SwitchDemoMode, x, y))     _touch_target = TouchTarget::SWITCH_DEMO;
    else if (hitsSwitch(ui_SwitchNightMode, x, y))    _touch_target = TouchTarget::SWITCH_NIGHT;
    else if (hitsSlider(ui_SliderBrightness, x, y))   _touch_target = TouchTarget::SLIDER_BRIGHTNESS;
    else if (this->waketimeEnabled() && hitsSlider(ui_SliderWaketime, x, y))
                                                      _touch_target = TouchTarget::SLIDER_WAKETIME;

    // A touch on the disabled wake-time slider is deliberately NOT claimed: it then falls
    // through to the normal gesture handling, exactly like a touch on empty space.
    return _touch_target != TouchTarget::NONE;
}

// Live slider drag: map the touch x across the slider's width to a value and apply it
// immediately. Touch is absolute, so no capture mode is needed. The switches are discrete
// controls — they act on release, not while the finger moves.
void SetupUI::onTouchDrag(int16_t x, int16_t y) {
    if (!_initialized) return;

    switch (_touch_target) {
        case TouchTarget::SLIDER_BRIGHTNESS: {
            int32_t pct = sliderValueAt(ui_SliderBrightness, x);
            if (pct < MIN_BRIGHTNESS_PERCENT) pct = MIN_BRIGHTNESS_PERCENT;
            if (pct > MAX_BRIGHTNESS_PERCENT) pct = MAX_BRIGHTNESS_PERCENT;
            _brightness_percent = (int8_t)pct;
            this->styleSlider(ui_SliderBrightness, true, true);   // feedback while held
            this->applyBrightness();
            this->updateUI();
            break;
        }

        case TouchTarget::SLIDER_WAKETIME: {
            int32_t s = sliderValueAt(ui_SliderWaketime, x);
            if (s < MIN_WAKETIME_S) s = MIN_WAKETIME_S;
            if (s > MAX_WAKETIME_S) s = MAX_WAKETIME_S;
            _waketime_s = (uint8_t)s;
            this->styleSlider(ui_SliderWaketime, true, true);
            this->updateUI();
            break;
        }

        default:
            break;
    }
}

// Claimed touch released. Sliders: persist the value once, off the hot path. Switches:
// flip the choice, repaint and persist immediately — a single deliberate gesture, and
// onLeave() never runs on the way into deep sleep, so a later save would lose it.
void SetupUI::onTouchEnd() {
    if (!_initialized) return;

    switch (_touch_target) {
        case TouchTarget::SLIDER_BRIGHTNESS:
            saveChar(NVS_KEY_BRIGHTNESS, _brightness_percent);
            break;

        case TouchTarget::SLIDER_WAKETIME:
            saveChar(NVS_KEY_WAKETIME, (int8_t)_waketime_s);
            Serial.printf("[setup] wake time %u s\n", _waketime_s);
            break;

        case TouchTarget::SWITCH_PREVENT:
            _prevent_sleep = !_prevent_sleep;
            saveBool(NVS_KEY_PREVENT_SLEEP, _prevent_sleep);
            Serial.printf("[setup] prevent sleep %s\n", _prevent_sleep ? "ON" : "OFF");
            break;

        case TouchTarget::SWITCH_DEMO:
            _demo_mode = !_demo_mode;
            saveBool(NVS_KEY_DEMO, _demo_mode);
            Serial.printf("[setup] demo mode %s\n", _demo_mode ? "ON" : "OFF");
            break;

        case TouchTarget::SWITCH_NIGHT:
            _night_mode = !_night_mode;
            saveBool(NVS_KEY_NIGHT, _night_mode);
            Serial.printf("[setup] night mode %s\n", _night_mode ? "ON" : "OFF");
            break;

        default:
            break;
    }
    _touch_target = TouchTarget::NONE;
    this->applyVisuals();   // drag highlight off, switch states, wake-time enable
}

// === P R I V A T E ===

int32_t SetupUI::sliderValueAt(lv_obj_t* slider, int16_t x) {
    lv_area_t a;
    lv_obj_get_coords(slider, &a);
    const int32_t lo = lv_slider_get_min_value(slider);
    const int32_t hi = lv_slider_get_max_value(slider);
    const int32_t span = (int32_t)a.x2 - (int32_t)a.x1;
    if (span <= 0) return lo;
    int32_t cx = x;
    if (cx < a.x1) cx = a.x1;
    if (cx > a.x2) cx = a.x2;
    return lo + ((cx - a.x1) * (hi - lo) + span / 2) / span;
}

bool SetupUI::hitsSwitch(lv_obj_t* sw, int16_t x, int16_t y) {
    lv_area_t a;
    lv_obj_get_coords(sw, &a);
    return x >= (a.x1 - TOUCH_PAD_PX) && x <= (a.x2 + TOUCH_PAD_PX) &&
           y >= (a.y1 - TOUCH_PAD_PX) && y <= (a.y2 + TOUCH_PAD_PX);
}

// Sliders are padded vertically only: horizontally the finger position IS the value.
bool SetupUI::hitsSlider(lv_obj_t* slider, int16_t x, int16_t y) {
    lv_area_t a;
    lv_obj_get_coords(slider, &a);
    return x >= a.x1 && x <= a.x2 &&
           y >= (a.y1 - TOUCH_PAD_PX) && y <= (a.y2 + TOUCH_PAD_PX);
}

void SetupUI::applyVisuals() {
    const bool b_active = (_touch_target == TouchTarget::SLIDER_BRIGHTNESS);
    const bool w_active = (_touch_target == TouchTarget::SLIDER_WAKETIME);
    this->styleSlider(ui_SliderBrightness, b_active, true);
    this->styleSlider(ui_SliderWaketime, w_active, this->waketimeEnabled());

    // LV_STATE_DISABLED is set for correctness (anything that asks LVGL sees it); the
    // look is painted explicitly above, since the theme's disabled style is not ours.
    if (this->waketimeEnabled()) lv_obj_remove_state(ui_SliderWaketime, LV_STATE_DISABLED);
    else                         lv_obj_add_state(ui_SliderWaketime, LV_STATE_DISABLED);

    this->styleSwitch(ui_SwitchPreventSleep, _prevent_sleep);
    this->styleSwitch(ui_SwitchDemoMode, _demo_mode);
    this->styleSwitch(ui_SwitchNightMode, _night_mode);
}

// Slider look, code-owned (overrides the SquareLine colors): track, then indicator + knob
// in amber while dragged, base grey at rest, dark grey when disabled. Styles are set for
// the DEFAULT and DISABLED states alike, so the theme's own disabled look never shows.
void SetupUI::styleSlider(lv_obj_t* slider, bool active, bool enabled) {
    const lv_color_t track = lv_color_hex(this->color(SLIDER_TRACK));
    const lv_color_t c = lv_color_hex(this->color(!enabled ? SLIDER_OFF
                                                  : active ? ADJUST_ACCENT
                                                           : SLIDER_BASE));
    for (lv_state_t st : { (lv_state_t)LV_STATE_DEFAULT, (lv_state_t)LV_STATE_DISABLED }) {
        lv_obj_set_style_bg_color(slider, track, LV_PART_MAIN | st);
        lv_obj_set_style_bg_color(slider, c, LV_PART_INDICATOR | st);
        lv_obj_set_style_bg_color(slider, c, LV_PART_KNOB | st);
    }
}

// Switch look and state. The SquareLine export ships no styles for these widgets, so the
// look is entirely code-owned. The widget's CHECKED state is driven from our own flag:
// the switches are not wired to an LVGL input device (touch is dispatched by the app), so
// this is the only thing that moves them. Track and knob get the off/on color directly
// (both parts, both states), so no theme color can show at the edges.
void SetupUI::styleSwitch(lv_obj_t* sw, bool checked) {
    const uint32_t knob  = checked ? (_night_mode ? NIGHT_ON_KNOB  : SWITCH_ON_KNOB)
                                   : (_night_mode ? NIGHT_OFF_KNOB : SWITCH_OFF_KNOB);
    const uint32_t track = checked ? (_night_mode ? NIGHT_ON_TRACK  : SWITCH_ON_TRACK)
                                   : (_night_mode ? NIGHT_OFF_TRACK : SWITCH_OFF_TRACK);

    for (lv_state_t st : { (lv_state_t)LV_STATE_DEFAULT, (lv_state_t)LV_STATE_CHECKED }) {
        lv_obj_set_style_bg_color(sw, lv_color_hex(track), LV_PART_MAIN | st);
        lv_obj_set_style_bg_color(sw, lv_color_hex(track), LV_PART_INDICATOR | st);
        lv_obj_set_style_bg_color(sw, lv_color_hex(knob),  LV_PART_KNOB | st);
        lv_obj_set_style_bg_opa(sw, 255, LV_PART_MAIN | st);
        lv_obj_set_style_bg_opa(sw, 255, LV_PART_INDICATOR | st);
        lv_obj_set_style_bg_opa(sw, 255, LV_PART_KNOB | st);
    }

    if (checked) lv_obj_add_state(sw, LV_STATE_CHECKED);
    else         lv_obj_remove_state(sw, LV_STATE_CHECKED);
}

void SetupUI::applyBrightness() {
    _brightness.set(this->percentToLevel(this->effectivePercent()));
}

int8_t SetupUI::effectivePercent() const {
    return (_brightness_percent < _ceiling_percent) ? _brightness_percent : _ceiling_percent;
}

// Slider positions and the wake-time label. ui_LabelBrightnessValue is hidden in the
// export and intentionally left alone (the slider already shows the level).
void SetupUI::updateUI() {
    lv_slider_set_value(ui_SliderBrightness, _brightness_percent, LV_ANIM_OFF);
    lv_slider_set_value(ui_SliderWaketime, _waketime_s, LV_ANIM_OFF);
    char buf[24];
    snprintf(buf, sizeof(buf), "Wake time %u s", _waketime_s);
    lv_label_set_text(ui_LabelWaketime, buf);
}

int8_t SetupUI::loadChar(const char* key, int8_t def) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    int8_t v = prefs.getChar(key, def);
    prefs.end();
    return v;
}

void SetupUI::saveChar(const char* key, int8_t v) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putChar(key, v);
    prefs.end();
}

bool SetupUI::loadBool(const char* key, bool def) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    bool v = prefs.getBool(key, def);
    prefs.end();
    return v;
}

void SetupUI::saveBool(const char* key, bool v) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBool(key, v);
    prefs.end();
}

uint8_t SetupUI::percentToLevel(int8_t percent) const {
    return (uint8_t)((int)percent * 255 / 100);
}
