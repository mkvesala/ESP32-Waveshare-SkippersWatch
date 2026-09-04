#pragma once

#include <Arduino.h>
#include <esp_sleep.h>
#include "BatteryMonitor.h"

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
// It is also the POLICY owner for everything that can force a sleep decision. The
// measured budget (2.16 mA sleep floor, ~145 mA active, 250 mAh cell) is comfortable
// for the 24 h + nightly-charge requirement, so the remaining risks are failure modes,
// not averages. Four guards, all here:
//
//   1. MAX_AWAKE_MS  — an ABSOLUTE ceiling on one awake session that notifyActivity()
//      cannot reset. A stuck input source (bouncing button, latched touch INT, a UI
//      path calling notifyActivity() per frame) would otherwise hold ~145 mA and empty
//      the 250 mAh cell in under two hours, silently.
//   2. Task watchdog — the ceiling only fires while loop() still runs; a HUNG loop
//      needs hardware. A trip resets the SoC, which is a recoverable cold boot.
//   3. Low-charge ladder — BatteryMonitor::Level shortens the display timeout, clamps
//      the backlight, and finally forces a protective sleep before the cell reaches
//      its own protection cutoff.
//   4. Stuck wake-button guard — ext0 is LEVEL-triggered, so a button held down at
//      sleep entry (pocket pressure on the EC11) would wake the watch instantly, over
//      and over. armWakeSource() then arms a timer instead and rechecks cheaply.
//
// Model C (light-listen window, accel wrist-raise) was evaluated and rejected — see
// the power-model notes. This class stays Model B.

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

    // Stuck-wake-button recheck. Call immediately after releaseHolds(), before ANY rail,
    // display, UI or radio init. On a timer wake it re-reads the wake button and goes
    // straight back to sleep without lighting anything up (~0.003 mAh per check), so a
    // held button costs a trickle instead of a 145 mA wake loop. Returns false on every
    // other wake cause (normal boot continues); the true branch never actually returns.
    bool handleTimerRecheck(esp_sleep_wakeup_cause_t cause);

    // Battery source for the low-charge ladder and the USB stay-awake rule. Call before
    // begin(). Null is tolerated: the class then behaves exactly as it did pre-ladder.
    void attachBattery(const BatteryMonitor* battery) { _battery = battery; }

    // Call once at end of app.begin(); records why we woke, starts the idle timer and
    // the awake ceiling, and subscribes the Arduino loop task to the task watchdog
    void begin(esp_sleep_wakeup_cause_t cause);

    // Reset the display-on timer — call on any user interaction (knob/button/touch).
    // Deliberately does NOT touch the awake ceiling.
    void notifyActivity();

    // Call every loop; feeds the watchdog and, when the display-on timeout or the awake
    // ceiling elapses, gates peripherals and enters deep sleep (this call does not
    // return — the SoC reboots on next wake)
    void loop(uint32_t now);

    // Protective shutdown on a near-empty cell. Callable even if begin() never ran (the
    // cutoff can be detected during the wake sequence, before the radio is brought up).
    // Does not return.
    void enterProtectiveSleep();

    // Display-on window for the current charge level (shorter as the cell empties)
    uint32_t displayTimeoutMs() const;

    // Backlight clamp (percent) for the current charge/charging state — the app hands
    // this to SetupUI::setBrightnessCeiling() each loop
    int8_t brightnessCeiling() const;

    // Same policy, usable before begin()/attachBattery() during the wake sequence so the
    // backlight ramps straight to the clamped target instead of flashing bright first
    static int8_t backlightCeilingFor(BatteryMonitor::Level level);

    bool wokeFromButton() const { return _cause == ESP_SLEEP_WAKEUP_EXT0; }
    PowerState state() const { return _state; }

    // Display-on window before returning to deep sleep (short for Model B endurance).
    // This is the healthy-cell case; the ladder shortens it further — see displayTimeoutMs().
    static constexpr uint32_t DISPLAY_TIMEOUT_MS = 8000;

    // Absolute ceiling on one awake session, battery only. notifyActivity() cannot
    // extend it. Generous enough for brightness adjustment and a long look; a stuck
    // input source therefore costs ~0.5 mAh per wake instead of the whole cell.
    static constexpr uint32_t MAX_AWAKE_MS = 120000;

    // On USB there is no endurance to protect: stay awake so the watch works as a
    // docked display. The panel is dimmed (not slept) after USB_DIM_IDLE_MS.
    static constexpr bool     STAY_AWAKE_ON_USB = true;
    static constexpr uint32_t USB_DIM_IDLE_MS   = 60000;
    static constexpr int8_t   USB_DIM_PERCENT   = 5;

private:

    // ramp_backlight=false for sleep paths that run before LEDC is attached
    // (the timer recheck), where ledcRead/ledcDetach would act on an unattached pin
    void enterDeepSleep(bool ramp_backlight = true);
    void rampBacklightDown();
    void gatePeripherals();
    void armWakeSource();
    void initWatchdog();

    bool chargingNow() const { return _battery != nullptr && _battery->charging(); }
    BatteryMonitor::Level levelNow() const {
        return (_battery != nullptr) ? _battery->level() : BatteryMonitor::Level::OK;
    }

    uint8_t _backlight_pin;
    uint8_t _lcd_power_pin;
    uint8_t _periph_power_pin;

    const BatteryMonitor* _battery = nullptr;

    esp_sleep_wakeup_cause_t _cause = ESP_SLEEP_WAKEUP_UNDEFINED;
    PowerState _state = PowerState::WAKING;
    uint32_t _last_activity = 0;
    uint32_t _wake_ms = 0;          // start of this awake session (the ceiling's origin)
    bool _was_charging = false;     // for the unplug edge (grants a fresh display window)
    bool _wdt_armed = false;

    // Encoder push-button — RTC-capable, zero standing draw (a mechanical switch),
    // the deep-sleep wake source (ext0, active-low). Deep-sleep touch-wake was tried
    // and found infeasible: the AXS5106L INT rests low and deep-sleep ext1 is
    // single-mode level-triggered, so it can't be mixed with the active-low button
    // (it would need a light-sleep conversion — see [[build-status]]). Parked.
    static constexpr uint8_t WAKE_BUTTON_PIN = 18;

    // Stuck-button guard: how long to sleep before rechecking a held wake button
    static constexpr uint64_t STUCK_BUTTON_RECHECK_US = 30ULL * 1000ULL * 1000ULL;

    // Task watchdog on the Arduino loop task. Long enough that no legitimate blocking
    // stretch (LVGL render, NVS commit, backlight ramp) can trip it.
    static constexpr uint32_t WDT_TIMEOUT_MS = 30000;

    // Display-on windows per charge level (ms). Shortening the window is what actually
    // saves charge on this board: active baseline is ~145 mA and the backlight adder at
    // 20 % is only ~10 mA, so 8 s → 3 s buys far more than any brightness clamp.
    static constexpr uint32_t TIMEOUT_WARN_MS     = 5000;
    static constexpr uint32_t TIMEOUT_CRITICAL_MS = 3000;

    // Backlight ceilings per charge level (percent). Small change, kept because it is free.
    static constexpr int8_t CEILING_NONE     = 100;
    static constexpr int8_t CEILING_CRITICAL = 10;
    static constexpr int8_t CEILING_CUTOFF   = 5;

    // Backlight soft ramp-down: steps × delay spreads the load-shed (no hard step)
    static constexpr uint8_t  RAMP_STEPS = 16;
    static constexpr uint32_t RAMP_STEP_MS = 8;

};
