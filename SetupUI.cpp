#include "SetupUI.h"
#include "ui.h"

// === P U B L I C ===

SetupUI::SetupUI(uint8_t backlight_pin)
    : _backlight_pin(backlight_pin) {}

lv_obj_t* SetupUI::getLvglScreen() const {
    return ui_ContainerSetup;
}

void SetupUI::begin() {
    if (_initialized) return;
    _brightness_percent = this->loadBrightness();
    // Do NOT light the backlight here — the panel stays dark through init/first
    // render; the app calls rampBacklightUp() afterwards (staggered wake).
    this->updateUI();
    this->setState(SetupState::IDLE);
    _initialized = true;
}

// Soft-ramp backlight 0 → stored brightness (PWM soft-start, never a hard step)
void SetupUI::rampBacklightUp() {
    uint8_t target = this->percentToPwm(_brightness_percent);
    uint8_t step = (target / 16) > 0 ? (target / 16) : 1;
    for (uint16_t d = 0; d < target; d += step) {
        ledcWrite(_backlight_pin, d);
        delay(8);
    }
    ledcWrite(_backlight_pin, target);
}

void SetupUI::update() {
    if (!_initialized) return;
    if (_state != SetupState::ADJUSTING) return;
    // Autosave + return to IDLE after inactivity (batches the NVS write off the hot path)
    if ((long)(millis() - _last_rotation_time) >= (long)AUTOSAVE_TIMEOUT_MS) {
        this->saveBrightness();
        this->setState(SetupState::IDLE);
    }
}

void SetupUI::onButtonPress() {
    if (!_initialized) return;
    if (_state == SetupState::IDLE) {
        this->setState(SetupState::ADJUSTING);
        _last_rotation_time = millis();
    } else {
        this->saveBrightness();
        this->setState(SetupState::IDLE);
    }
}

void SetupUI::onRotation(int8_t dir) {
    if (!_initialized) return;
    _brightness_percent += dir * BRIGHTNESS_STEP;
    if (_brightness_percent < MIN_BRIGHTNESS_PERCENT) _brightness_percent = MIN_BRIGHTNESS_PERCENT;
    if (_brightness_percent > MAX_BRIGHTNESS_PERCENT) _brightness_percent = MAX_BRIGHTNESS_PERCENT;
    this->applyBrightness();
    this->updateUI();
    _last_rotation_time = millis();
}

// Consume rotation (so the knob adjusts brightness instead of switching pages) while adjusting
bool SetupUI::interceptsRotation() const {
    return _state == SetupState::ADJUSTING;
}

void SetupUI::onLeave() {
    if (_state == SetupState::ADJUSTING) {
        this->saveBrightness();
        this->setState(SetupState::IDLE);
    }
}

// Claim a touch-down that lands on the brightness slider (with a little vertical
// padding so it is finger-friendly), so the app routes it here as a drag instead of
// treating it as a swipe. Coordinates are panel pixels; the slider's on-screen box
// comes from LVGL so this tracks the SquareLine layout automatically.
bool SetupUI::interceptsTouchAt(int16_t x, int16_t y) const {
    if (!_initialized) return false;
    lv_area_t a;
    lv_obj_get_coords(ui_SliderBrightness, &a);
    return x >= a.x1 && x <= a.x2 &&
           y >= (a.y1 - TOUCH_SLIDER_PAD_PX) && y <= (a.y2 + TOUCH_SLIDER_PAD_PX);
}

// Live slider drag: map the touch x across the slider's width to a percentage and
// apply immediately (LEDC + label), reusing the same helpers as the knob path. The
// amber accent shows while dragging so the interaction reads the same as the knob's
// ADJUSTING mode — without entering it (touch is absolute, no capture needed).
void SetupUI::onTouchDrag(int16_t x, int16_t y) {
    if (!_initialized) return;
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
    this->setAdjustVisual(true);   // visual feedback during the drag (state unchanged)
    this->applyBrightness();
    this->updateUI();
}

// Drag released: persist once (off the hot path, like the knob autosave) and restore
// the slider visual to match the encoder state so the two input paths can't disagree.
void SetupUI::onTouchEnd() {
    if (!_initialized) return;
    this->saveBrightness();
    this->setAdjustVisual(_state == SetupState::ADJUSTING);
}

// === P R I V A T E ===

// Only place _state is written — keeps the adjust-mode visual in lockstep with state
void SetupUI::setState(SetupState new_state) {
    _state = new_state;
    this->setAdjustVisual(new_state == SetupState::ADJUSTING);
}

// Paint the slider indicator + knob: ADJUST_ACCENT while ADJUSTING (mode is obvious),
// SLIDER_BASE at rest. Both colors are code-owned, so the resting look is tunable too.
void SetupUI::setAdjustVisual(bool active) {
    lv_color_t c = lv_color_hex(active ? ADJUST_ACCENT : SLIDER_BASE);
    lv_obj_set_style_bg_color(ui_SliderBrightness, c, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ui_SliderBrightness, c, LV_PART_KNOB);
}

void SetupUI::applyBrightness() {
    ledcWrite(_backlight_pin, this->percentToPwm(_brightness_percent));
}

void SetupUI::updateUI() {
    lv_slider_set_value(ui_SliderBrightness, _brightness_percent, LV_ANIM_OFF);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", _brightness_percent);
    lv_label_set_text(ui_LabelBrightnessValue, buf);
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

uint8_t SetupUI::percentToPwm(int8_t percent) const {
    return (uint8_t)((int)percent * 255 / 100);
}
