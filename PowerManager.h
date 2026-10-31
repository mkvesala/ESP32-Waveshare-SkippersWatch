#pragma once

#include <Arduino.h>
#include <esp_sleep.h>
#include <Arduino_GFX_Library.h>
#include "BatteryMonitor.h"
#include "PanelBrightness.h"
#include "TouchInput.h"

class PmuRails;

// === C L A S S  P O W E R M A N A G E R ===
//
// - Model B (deep-sleep wake-to-fetch) power owner for the SkippersWatch.
// - Deep sleep is the resting default; the PWR button (GPIO10 = SYS_OUT, RTC-capable,
//   ACTIVE-HIGH) wakes the watch, which is a FULL REBOOT — so all cached readings start stale
//   ("---") and are refreshed only when a fresh ESP-NOW broadcast lands. That
//   full-reboot-on-wake is itself the never-show-stale freshness reset.
// - This class manages the display-on timeout and the peripheral-gating +
//   wake-arming sleep entry. The staggered WAKE bring-up (panel dark → radio →
//   listen window → render → brightness ramp) lives in WatchApplication::begin(),
//   which runs on every wake.
// - Owned by: WatchApplication
//
// It is also the POLICY owner for everything that can force a sleep decision. The
// policy is carried over unchanged from the CrowPanel build, where the measured budget
// was comfortable for the 24 h + nightly-charge requirement; this board's sleep floor
// and active draw are still to be measured [M]. The remaining risks are failure modes,
// not averages. Four guards, all here:
//
//   1. MAX_AWAKE_MS  — an ABSOLUTE ceiling on one awake session that notifyActivity()
//      cannot reset. A stuck input source (bouncing button, latched touch INT, a UI
//      path calling notifyActivity() per frame) would otherwise hold active current
//      and empty the cell in a few hours, silently.
//   2. Task watchdog — the ceiling only fires while loop() still runs; a HUNG loop
//      needs hardware. A trip resets the SoC, which is a recoverable cold boot.
//   3. Low-charge ladder — BatteryMonitor::Level shortens the display timeout, clamps
//      the backlight, and finally forces a protective sleep before the cell reaches
//      its own protection cutoff.
//   4. Stuck wake-button guard — ext0/ext1 are LEVEL-triggered, so a button held down at
//      sleep entry (strap/sleeve pressure on the PWR key) would wake the watch instantly, over
//      and over. armWakeSource() then arms a timer instead and rechecks cheaply.
//
// Model C (light-listen window, accel wrist-raise) was evaluated and rejected — see
// the power-model notes. This class stays Model B.

class PowerManager {

public:

    // States within one awake session (deep sleep itself = powered down, next boot)
    enum class PowerState { WAKING, ACTIVE, DISPLAY_HOLD, SLEEP_PREP };

    explicit PowerManager(PanelBrightness& brightness);

    // Release GPIO holds latched before the previous deep sleep (panel/touch reset lines,
    // amp enable). MUST be called at the very START of app.begin(), before the display
    // and touch are re-initialised — otherwise those pads stay frozen.
    void releaseHolds();

    // Hand over the peripherals put to sleep at sleep entry. Until attached (the stuck-
    // button timer path), sleep entry only latches the pins.
    void attachPeripherals(Arduino_OLED* panel, TouchInput* touch) {
        _panel = panel;
        _touch = touch;
    }

    // AXP2101 rail mechanism, switched to its sleep state at sleep entry. Null on the
    // stuck-button timer path, which sleeps before the PMIC is brought up — the rails
    // then simply stay in the state the previous sleep left them in.
    void attachRails(PmuRails* rails) { _rails = rails; }

    // Stuck-wake-button recheck. Call immediately after releaseHolds(), before ANY rail,
    // display, UI or radio init. On a timer wake it re-reads the wake button and goes
    // straight back to sleep without lighting anything up (~0.003 mAh per check), so a
    // held button costs a trickle instead of an active-current wake loop. Returns false on every
    // other wake cause (normal boot continues); the true branch never actually returns.
    bool handleTimerRecheck(esp_sleep_wakeup_cause_t cause);

    // Battery source for the low-charge ladder and the USB stay-awake rule. Call before
    // begin(). Null is tolerated: the class then behaves exactly as it did pre-ladder.
    void attachBattery(const BatteryMonitor* battery) { _battery = battery; }

    // Pre-sleep flush hook. Called ONCE at the very top of the deep-sleep entry, before
    // the backlight ramp — the radio is already idle and no render is in flight there, so
    // a flash (NVS) write placed here cannot stack onto another current spike. Kept as a
    // plain function pointer + context rather than std::function: no heap, no exceptions.
    //
    // Deliberately registered only at the END of app.begin(), so it is still null on the
    // stuck-wake-button recheck path (handleTimerRecheck() sleeps before begin() runs).
    // That path must touch nothing — not even flash.
    using SleepHook = void (*)(void* ctx);
    void setPreSleepHook(SleepHook hook, void* ctx) { _sleep_hook = hook; _sleep_ctx = ctx; }

    // User's "prevent sleep" choice (SetupUI switch), pushed in every loop by the app.
    // Storing it is not the same as honouring it — see preventSleepActive().
    void setPreventSleep(bool on) { _prevent_sleep = on; }

    // User's wake time (SetupUI slider), pushed in every loop by the app. The low-charge
    // ladder may shorten it further — see displayTimeoutMs().
    void setDisplayTimeout(uint32_t ms) { _user_timeout_ms = ms; }

    // Call once at end of app.begin(); records why we woke, starts the idle timer and
    // the awake ceiling, and subscribes the Arduino loop task to the task watchdog
    void begin(esp_sleep_wakeup_cause_t cause);

    // Reset the display-on timer — call on any user interaction (touch).
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

    bool wokeFromButton() const {
        return _cause == ESP_SLEEP_WAKEUP_EXT0 || _cause == ESP_SLEEP_WAKEUP_EXT1;
    }
    PowerState state() const { return _state; }

    // Default display-on window before returning to deep sleep, used until the app pushes
    // the user's wake time. The ladder shortens it further — see displayTimeoutMs().
    static constexpr uint32_t DISPLAY_TIMEOUT_MS = 10000;

    // Absolute ceiling on one awake session, battery only. notifyActivity() cannot
    // extend it. Generous enough for brightness adjustment and a long look; a stuck
    // input source therefore costs at most 120 s of active current per wake instead of
    // the whole cell.
    static constexpr uint32_t MAX_AWAKE_MS = 120000;

    // On USB there is no endurance to protect: stay awake so the watch works as a
    // docked display, with no display timeout AND no awake ceiling.
    static constexpr bool STAY_AWAKE_ON_USB = true;

    // Idle dim shared by BOTH stay-awake modes (USB and the prevent-sleep switch):
    // the panel is dimmed rather than slept after this long without input. Named for
    // the behaviour, not the power source — prevent sleep runs it on battery too.
    static constexpr uint32_t STAYAWAKE_DIM_IDLE_MS = 60000;
    static constexpr int8_t   STAYAWAKE_DIM_PERCENT = 5;

    // --- Deep-sleep floor measures (2026-10-31) ---
    // The first battery test drained ~7-8 mA in deep sleep (400 mAh, 100 -> 54 % in
    // 25 h) against a ~2 mA target. Each measure has its own switch so the floor can be
    // bisected over nights with the Status screen's "Avg drain" row, the only meter
    // available. Expected effect per measure is [M] until that row says otherwise.
    //
    // ALDO2 off in sleep → DSI_PWR_EN low, the AMOLED module's own converter off (PmuRails)
    static constexpr bool SLEEP_CUT_PANEL_POWER = true;
    // QSPI and I2S pads driven to a defined level and held, instead of floating into
    // inputs that are still powered (panel VDDIO and ES8311 sit on VCC3V3)
    static constexpr bool SLEEP_DEFINE_PADS = true;
    // Rails with no load (ALDO3/4, BLDO1/2, CPUSLDO, DLDO1/2) off on every wake (PmuRails)
    static constexpr bool UNUSED_RAILS_OFF = true;
    // ES7210 + ES8311 into their drivers' power-down state on every wake (PmuRails)
    static constexpr bool AUDIO_CODECS_OFF = true;
    // Wake on ext1 (RTC controller) instead of ext0, which keeps the RTC_PERIPH domain
    // powered. GPIO10 is driven by T1/R11, so no internal pull is needed.
    static constexpr bool WAKE_ON_EXT1 = true;

private:

    // ramp_brightness=false for sleep paths that run before the panel is initialised
    // (the timer recheck)
    void enterDeepSleep(bool ramp_brightness = true);
    void rampBrightnessDown();
    void gatePeripherals();
    void definePads();
    void armWakeSource();
    void initWatchdog();

    bool chargingNow() const { return _battery != nullptr && _battery->charging(); }

    // Whether the user's prevent-sleep choice may actually be honoured right now. The
    // low-charge ladder wins from CRITICAL down: holding active current on a cell that is
    // already under 10 % is exactly what the ladder exists to stop. The stored setting
    // is never rewritten, so it resumes by itself once the cell recovers — same
    // principle as the backlight ceiling, which clamps without touching the slider.
    // (CUTOFF is handled earlier still, by WatchApplication::checkBatteryCutoff().)
    bool preventSleepActive() const {
        return _prevent_sleep && this->levelNow() < BatteryMonitor::Level::CRITICAL;
    }
    BatteryMonitor::Level levelNow() const {
        return (_battery != nullptr) ? _battery->level() : BatteryMonitor::Level::OK;
    }

    PanelBrightness& _brightness;
    Arduino_OLED* _panel = nullptr;
    TouchInput*   _touch = nullptr;
    PmuRails*     _rails = nullptr;

    const BatteryMonitor* _battery = nullptr;

    SleepHook _sleep_hook = nullptr;
    void*     _sleep_ctx  = nullptr;

    bool _prevent_sleep = false;   // user's SetupUI choice, pushed in every loop
    uint32_t _user_timeout_ms = DISPLAY_TIMEOUT_MS;   // user's wake time, pushed in every loop

    esp_sleep_wakeup_cause_t _cause = ESP_SLEEP_WAKEUP_UNDEFINED;
    PowerState _state = PowerState::WAKING;
    uint32_t _last_activity = 0;
    uint32_t _wake_ms = 0;          // start of this awake session (the ceiling's origin)
    bool _was_charging = false;     // for the unplug edge (grants a fresh display window)
    bool _wdt_armed = false;

    // PWR button state line (SYS_OUT) — RTC-capable, ACTIVE-HIGH (high = pressed), the
    // deep-sleep wake source (ext1 ANY_HIGH, or ext0 level 1 — see WAKE_ON_EXT1). Touch cannot wake from deep sleep on this
    // board: the FT3168 INT is GPIO38, not an RTC pin. The same key is the AXP2101 power
    // key: ~6 s held cuts power in hardware, whatever the firmware does.
    static constexpr uint8_t WAKE_BUTTON_PIN = 10;

    // Pins latched through deep sleep: panel and touch reset lines held HIGH so both chips
    // stay in their sleep modes (a floating reset could reboot them into active mode), and
    // the speaker amp enable held LOW.
    static constexpr uint8_t PIN_LCD_RST  = 8;
    static constexpr uint8_t PIN_TP_RST   = TouchInput::TOUCH_RST_PIN;
    static constexpr uint8_t PIN_PA_CTRL  = 46;

    // Pads latched through deep sleep when SLEEP_DEFINE_PADS is on. RTC pads (GPIO0-21)
    // need gpio_hold_en() each; digital pads are covered by gpio_deep_sleep_hold_en().
    // Panel QSPI: CS idle high, clock and data low.
    static constexpr uint8_t PIN_LCD_CS   = 12;
    static constexpr uint8_t PIN_LCD_SCK  = 11;
    static constexpr uint8_t PIN_LCD_SIO[4] = { 4, 5, 6, 7 };
    // I2S outputs towards the ES8311 (MCLK is an RTC pad, the rest are digital), driven
    // low. ASDOUT (GPIO42) is an ES7210 OUTPUT, so it gets a pull-down, never a driver.
    static constexpr uint8_t PIN_I2S_MCLK  = 16;
    static constexpr uint8_t PIN_I2S_OUT[3] = { 40 /*DSDIN*/, 41 /*SCLK*/, 45 /*LRCK*/ };
    static constexpr uint8_t PIN_I2S_ASDOUT = 42;

    // Stuck-button guard: how long to sleep before rechecking a held wake button
    static constexpr uint64_t STUCK_BUTTON_RECHECK_US = 30ULL * 1000ULL * 1000ULL;

    // Task watchdog on the Arduino loop task. Long enough that no legitimate blocking
    // stretch (LVGL render, NVS commit, backlight ramp) can trip it.
    static constexpr uint32_t WDT_TIMEOUT_MS = 30000;

    // Brightness ceilings per charge level (percent). Small change, kept because it is free.
    static constexpr int8_t CEILING_NONE     = 100;
    static constexpr int8_t CEILING_CRITICAL = 10;
    static constexpr int8_t CEILING_CUTOFF   = 5;

    // Brightness soft ramp-down: steps × delay spreads the load-shed (no hard step)
    static constexpr uint8_t  RAMP_STEPS = 16;
    static constexpr uint32_t RAMP_STEP_MS = 8;

};
