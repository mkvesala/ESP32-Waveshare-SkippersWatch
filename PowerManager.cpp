#include "PowerManager.h"
#include <driver/gpio.h>
#include <driver/rtc_io.h>

// === P U B L I C ===

PowerManager::PowerManager(uint8_t backlight_pin,
                           uint8_t lcd_power_pin,
                           uint8_t periph_power_pin)
    : _backlight_pin(backlight_pin)
    , _lcd_power_pin(lcd_power_pin)
    , _periph_power_pin(periph_power_pin) {}

// Release the pin holds latched before the previous deep sleep so the rails/backlight
// are controllable again. Called FIRST in app.begin(), before initPowerRails()/initDisplay()
// — if these stayed held, the LCD rail would remain off and the panel would wake black.
void PowerManager::releaseHolds() {
    gpio_hold_dis((gpio_num_t)_lcd_power_pin);
    gpio_hold_dis((gpio_num_t)_periph_power_pin);
    gpio_hold_dis((gpio_num_t)_backlight_pin);
    gpio_deep_sleep_hold_dis();
}

void PowerManager::begin(esp_sleep_wakeup_cause_t cause) {
    _cause = cause;

    switch (cause) {
        case ESP_SLEEP_WAKEUP_EXT0:  Serial.println("[power] wake: button");   break;
        case ESP_SLEEP_WAKEUP_TIMER: Serial.println("[power] wake: timer");    break;
        default:                     Serial.println("[power] cold boot");      break;
    }

    _state = PowerState::DISPLAY_HOLD;
    _last_activity = millis();
}

void PowerManager::notifyActivity() {
    _last_activity = millis();
    _state = PowerState::ACTIVE;
}

void PowerManager::loop(uint32_t now) {
    // Overflow-safe elapsed check (same idiom as the rest of the codebase)
    if ((long)(now - _last_activity) < (long)DISPLAY_TIMEOUT_MS) {
        return;
    }
    _state = PowerState::SLEEP_PREP;
    this->enterDeepSleep();  // does not return
}

// === P R I V A T E ===

// SLEEP_PREP → deep sleep. Order: shed loads, gate rails, latch pins, arm wake, sleep.
void PowerManager::enterDeepSleep() {
    Serial.println("[power] entering deep sleep");
    Serial.flush();

    this->rampBacklightDown();   // soft-shed the biggest load first
    this->gatePeripherals();     // LCD power + peripheral rail off, latched
    this->armButtonWake();       // encoder button (GPIO18) wakes us

    esp_deep_sleep_start();       // full power-down; next wake re-runs setup()
}

// Backlight PWM soft ramp-down to 0 (never a hard step off)
void PowerManager::rampBacklightDown() {
    uint32_t duty = ledcRead(_backlight_pin);
    for (uint8_t i = 0; i < RAMP_STEPS; ++i) {
        duty = (duty > (255 / RAMP_STEPS)) ? (duty - (255 / RAMP_STEPS)) : 0;
        ledcWrite(_backlight_pin, duty);
        delay(RAMP_STEP_MS);
    }
    ledcWrite(_backlight_pin, 0);
    // Hand the pin back to plain GPIO so its low level can be latched through sleep
    ledcDetach(_backlight_pin);
    pinMode(_backlight_pin, OUTPUT);
    digitalWrite(_backlight_pin, LOW);
}

// Cut the LCD power rail (active-low → drive HIGH) and the shared peripheral rail,
// then latch every output level so nothing floats/back-powers during deep sleep.
void PowerManager::gatePeripherals() {
    pinMode(_lcd_power_pin, OUTPUT);
    digitalWrite(_lcd_power_pin, HIGH);    // active-low: HIGH = LCD rail OFF
    pinMode(_periph_power_pin, OUTPUT);
    digitalWrite(_periph_power_pin, LOW);  // shared peripheral rail OFF

    gpio_hold_en((gpio_num_t)_backlight_pin);
    gpio_hold_en((gpio_num_t)_lcd_power_pin);
    gpio_hold_en((gpio_num_t)_periph_power_pin);
    gpio_deep_sleep_hold_en();
}

// Arm the encoder push button (active-low) as an ext0 deep-sleep wake source
void PowerManager::armButtonWake() {
    rtc_gpio_init((gpio_num_t)WAKE_BUTTON_PIN);
    rtc_gpio_set_direction((gpio_num_t)WAKE_BUTTON_PIN, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis((gpio_num_t)WAKE_BUTTON_PIN);
    rtc_gpio_pullup_en((gpio_num_t)WAKE_BUTTON_PIN);     // released = high, press = low
    esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_BUTTON_PIN, 0);  // wake when low (pressed)
}
