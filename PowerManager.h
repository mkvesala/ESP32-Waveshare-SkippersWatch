#pragma once

#include <Arduino.h>
#include <esp_sleep.h>

// === C L A S S  P O W E R M A N A G E R ===
//
// - Model B (deep-sleep wake-to-fetch) power owner for the SkippersWatch.
// - Deep sleep is the resting default; the encoder button (GPIO18, RTC-capable)
//   wakes the watch, which is a FULL REBOOT — so all cached readings start stale
//   ("---") and are refreshed only when a fresh ESP-NOW broadcast lands. That
//   full-reboot-on-wake is itself the never-show-stale freshness reset.
// - This class manages the display-on timeout and the peripheral-gating +
//   wake-arming sleep entry. The staggered WAKE bring-up (CPU→panel→render→
//   backlight ramp→radio last) lives in CrowPanelApplication::begin(), which runs
//   on every wake.
// - Owned by: CrowPanelApplication
//
// Model C (light-listen window, accel wrist-raise, battery/SoC ladder) layers on
// top of this once the measured deep-sleep floor justifies it.

class PowerManager {

public:

    // States within one awake session (deep sleep itself = powered down, next boot)
    enum class PowerState { WAKING, ACTIVE, DISPLAY_HOLD, SLEEP_PREP };

    explicit PowerManager(uint8_t backlight_pin,
                          uint8_t lcd_power_pin,
                          uint8_t periph_power_pin);

    // Release GPIO holds latched before the previous deep sleep. MUST be called at the
    // very START of app.begin(), before the power rails / display are re-initialised —
    // otherwise those pads stay frozen (LCD rail off) and the panel wakes to black.
    void releaseHolds();

    // Call once at end of app.begin(); records why we woke and starts the idle timer
    void begin(esp_sleep_wakeup_cause_t cause);

    // Reset the display-on timer — call on any user interaction (knob/button)
    void notifyActivity();

    // Call every loop; when the display-on timeout elapses, gates peripherals and
    // enters deep sleep (this call does not return — the SoC reboots on next wake)
    void loop(uint32_t now);

    bool wokeFromButton() const { return _cause == ESP_SLEEP_WAKEUP_EXT0; }
    PowerState state() const { return _state; }

    // Display-on window before returning to deep sleep (short for Model B endurance)
    static constexpr uint32_t DISPLAY_TIMEOUT_MS = 8000;

private:

    void enterDeepSleep();
    void rampBacklightDown();
    void gatePeripherals();
    void armButtonWake();

    uint8_t _backlight_pin;
    uint8_t _lcd_power_pin;
    uint8_t _periph_power_pin;

    esp_sleep_wakeup_cause_t _cause = ESP_SLEEP_WAKEUP_UNDEFINED;
    PowerState _state = PowerState::WAKING;
    uint32_t _last_activity = 0;

    // Encoder button — RTC-capable wake source (see hardware_notes.md / RotaryEncoder)
    static constexpr uint8_t WAKE_BUTTON_PIN = 18;

    // Backlight soft ramp-down: steps × delay spreads the load-shed (no hard step)
    static constexpr uint8_t  RAMP_STEPS = 16;
    static constexpr uint32_t RAMP_STEP_MS = 8;

};
