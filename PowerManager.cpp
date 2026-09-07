#include "PowerManager.h"
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_task_wdt.h>

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

    // Hand the wake button back to the normal GPIO matrix: after an ext0 wake the pad is
    // still in RTC mode, where digitalRead() (used by the stuck-button guard) and the
    // encoder's own pinMode() are not meaningful.
    rtc_gpio_deinit((gpio_num_t)WAKE_BUTTON_PIN);
}

// Timer wake = the stuck-button guard rechecking, never a user request. Do the absolute
// minimum: read the button, sleep again. No rails, no panel, no UI, no radio — the whole
// visit is a few hundred ms at boot current, and armWakeSource() below decides whether
// the next sleep is armed on ext0 (released) or another timer (still held).
bool PowerManager::handleTimerRecheck(esp_sleep_wakeup_cause_t cause) {
    if (cause != ESP_SLEEP_WAKEUP_TIMER) return false;

    pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP);
    delay(5);   // let the pull-up settle before judging the level
    Serial.printf("[power] timer recheck: wake button %s\n",
                  (digitalRead(WAKE_BUTTON_PIN) == LOW) ? "still held" : "released");

    this->enterDeepSleep(false);   // LEDC was never attached on this path — no ramp
    return true;                   // unreachable: enterDeepSleep() does not return
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
    _wake_ms = _last_activity;      // the awake ceiling counts from here, and only here
    _was_charging = this->chargingNow();

    // Armed only now, at the END of the wake sequence: the rail/display init, the 200 ms
    // dark ESP-NOW listen window and the backlight ramp all run before this, so none of
    // them has to feed the watchdog.
    this->initWatchdog();
}

void PowerManager::notifyActivity() {
    _last_activity = millis();
    _state = PowerState::ACTIVE;
}

void PowerManager::loop(uint32_t now) {
    if (_wdt_armed) esp_task_wdt_reset();

    // Charging edge. On unplug, grant a fresh display window instead of sleeping
    // instantly on a _last_activity that may be minutes old (the watch was docked).
    const bool charging = this->chargingNow();
    if (charging != _was_charging) {
        _was_charging = charging;
        if (!charging) {
            Serial.println("[power] USB removed -> fresh display window");
            // The battery-powered session starts HERE. Restarting the ceiling matters:
            // after hours docked, _wake_ms is long past MAX_AWAKE_MS and the ceiling
            // below would sleep the watch instantly, cancelling the window just granted.
            _wake_ms = millis();
            this->notifyActivity();
        }
    }

    // On external power: never time out, never apply the awake ceiling. The panel is
    // dimmed instead (see brightnessCeiling()), so a docked watch stays readable.
    if (charging && STAY_AWAKE_ON_USB) return;

    // Absolute ceiling: fires regardless of how often notifyActivity() was called, so a
    // stuck input source cannot hold the board at ~145 mA until the cell is flat.
    if ((long)(now - _wake_ms) >= (long)MAX_AWAKE_MS) {
        Serial.println("[power] awake ceiling reached -> sleep");
        _state = PowerState::SLEEP_PREP;
        this->enterDeepSleep();   // does not return
    }

    // Overflow-safe elapsed check (same idiom as the rest of the codebase)
    if ((long)(now - _last_activity) < (long)this->displayTimeoutMs()) {
        return;
    }
    _state = PowerState::SLEEP_PREP;
    this->enterDeepSleep();  // does not return
}

// Near-empty cell: shut down before the cell's own protection cutoff does it for us.
// The button wake stays armed on purpose — the next press re-enters the same short
// cutoff cycle (~0.1 mAh) and plugging in USB restores a normal boot, whereas refusing
// to wake at all would read as a bricked watch.
void PowerManager::enterProtectiveSleep() {
    Serial.println("[power] LOW BATTERY -> protective sleep");
    _state = PowerState::SLEEP_PREP;
    this->enterDeepSleep();   // does not return
}

// Display-on window for the current charge level. Shortening this is the real lever on
// this board (active baseline ~145 mA dominates the backlight adder).
uint32_t PowerManager::displayTimeoutMs() const {
    switch (this->levelNow()) {
        case BatteryMonitor::Level::CRITICAL: return TIMEOUT_CRITICAL_MS;
        case BatteryMonitor::Level::WARN:     return TIMEOUT_WARN_MS;
        default:                              return DISPLAY_TIMEOUT_MS;
    }
}

int8_t PowerManager::brightnessCeiling() const {
    if (this->chargingNow()) {
        // Docked: full brightness while in use, dimmed (not slept) once left alone.
        // Any knob/touch activity calls notifyActivity() and restores it next loop.
        const bool idle = (long)(millis() - _last_activity) >= (long)USB_DIM_IDLE_MS;
        return idle ? USB_DIM_PERCENT : CEILING_NONE;
    }
    return backlightCeilingFor(this->levelNow());
}

int8_t PowerManager::backlightCeilingFor(BatteryMonitor::Level level) {
    switch (level) {
        case BatteryMonitor::Level::CUTOFF:   return CEILING_CUTOFF;
        case BatteryMonitor::Level::CRITICAL: return CEILING_CRITICAL;
        default:                              return CEILING_NONE;
    }
}

// === P R I V A T E ===

// SLEEP_PREP → deep sleep. Order: shed loads, gate rails, latch pins, arm wake, sleep.
void PowerManager::enterDeepSleep(bool ramp_backlight) {
    Serial.println("[power] entering deep sleep");
    Serial.flush();

    // Pre-sleep flush FIRST, while the radio is idle and no render is in flight: a flash
    // (NVS) write here is the only current spike in that instant. Null on the stuck-button
    // recheck path, which deliberately touches nothing.
    if (_sleep_hook != nullptr) _sleep_hook(_sleep_ctx);

    // Unsubscribe before the ramp so a shortened WDT timeout could never trip here
    if (_wdt_armed) {
        esp_task_wdt_delete(NULL);
        _wdt_armed = false;
    }

    if (ramp_backlight) this->rampBacklightDown();  // soft-shed the biggest load first
    this->gatePeripherals();     // LCD power + peripheral rail off, latched
    this->armWakeSource();       // encoder button (GPIO18), or a timer if it is held

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
    // Force the backlight pin low here too, not only in rampBacklightDown(): the timer
    // recheck path skips the ramp, and holding an undriven (floating) pad through deep
    // sleep could latch the backlight ON — the single biggest load, for 30 s at a time.
    pinMode(_backlight_pin, OUTPUT);
    digitalWrite(_backlight_pin, LOW);

    pinMode(_lcd_power_pin, OUTPUT);
    digitalWrite(_lcd_power_pin, HIGH);    // active-low: HIGH = LCD rail OFF
    pinMode(_periph_power_pin, OUTPUT);
    digitalWrite(_periph_power_pin, LOW);  // shared peripheral rail OFF

    gpio_hold_en((gpio_num_t)_backlight_pin);
    gpio_hold_en((gpio_num_t)_lcd_power_pin);
    gpio_hold_en((gpio_num_t)_periph_power_pin);
    gpio_deep_sleep_hold_en();
}

// Arm the encoder push button (active-low) as an ext0 deep-sleep wake source.
//
// ext0 is LEVEL-triggered: arming it while the button is already down (pocket pressure
// on the EC11) makes esp_deep_sleep_start() return immediately — a reboot loop at
// ~145 mA that would empty the cell in under two hours. So if the button is held, arm a
// timer instead and let handleTimerRecheck() look again in 30 s without lighting the
// watch up. Each recheck costs ~0.003 mAh; the sleep floor in between is the usual 2.16 mA.
void PowerManager::armWakeSource() {
    // Wake sources are cumulative within one boot — clear before arming so a timer
    // recheck can never leave a stale timer source alongside a freshly armed ext0.
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP);
    delayMicroseconds(200);

    if (digitalRead(WAKE_BUTTON_PIN) == LOW) {
        Serial.println("[power] arm: wake button held -> timer recheck in 30 s");
        Serial.flush();
        esp_sleep_enable_timer_wakeup(STUCK_BUTTON_RECHECK_US);
        return;
    }

    rtc_gpio_init((gpio_num_t)WAKE_BUTTON_PIN);
    rtc_gpio_set_direction((gpio_num_t)WAKE_BUTTON_PIN, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis((gpio_num_t)WAKE_BUTTON_PIN);
    rtc_gpio_pullup_en((gpio_num_t)WAKE_BUTTON_PIN);     // released = high, press = low
    esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_BUTTON_PIN, 0);  // wake when low (pressed)
}

// Subscribe the Arduino loop task to the task watchdog. The awake ceiling only fires
// while loop() still runs; this covers the case where it does not (a blocking bug in
// LVGL, I2C or a page adapter). A trip resets the SoC → cold boot → normal 8 s window,
// which is a far better outcome than a flat cell.
//
// RECONFIGURE FIRST, init only as a fallback: the Arduino core already initialises the
// TWDT at boot, so esp_task_wdt_init() would always take the "already initialized" path
// — and IDF logs that at E level before returning ESP_ERR_INVALID_STATE, which looks
// like a failure in the serial log even though the fallback handles it. This order is
// silent on this core and still correct on one built without TWDT init.
void PowerManager::initWatchdog() {
    esp_task_wdt_config_t cfg = {};
    cfg.timeout_ms     = WDT_TIMEOUT_MS;
    // Keep CPU0's idle task monitored: the Arduino core enables that by default
    // (CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y) and reconfiguring with a zero mask
    // would silently switch off protection that already existed. We only mean to
    // LENGTHEN the timeout (5 s → 30 s) for our own loop, not to remove a guard.
    cfg.idle_core_mask = (1 << 0);
    cfg.trigger_panic  = true;

    esp_err_t err = esp_task_wdt_reconfigure(&cfg);
    if (err == ESP_ERR_INVALID_STATE) err = esp_task_wdt_init(&cfg);
    if (err != ESP_OK) {
        Serial.printf("[power] WARN task watchdog init failed (%d) — no hang protection\n", err);
        return;
    }

    if (esp_task_wdt_add(NULL) == ESP_OK) {
        _wdt_armed = true;
        Serial.printf("[power] task watchdog armed (%lu ms)\n", (unsigned long)WDT_TIMEOUT_MS);
    }
}
