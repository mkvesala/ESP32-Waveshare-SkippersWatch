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
    _state = SetupState::IDLE;
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
        _state = SetupState::IDLE;
    }
}

void SetupUI::onButtonPress() {
    if (!_initialized) return;
    if (_state == SetupState::IDLE) {
        _state = SetupState::ADJUSTING;
        _last_rotation_time = millis();
    } else {
        this->saveBrightness();
        _state = SetupState::IDLE;
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
        _state = SetupState::IDLE;
    }
}

// === P R I V A T E ===

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
