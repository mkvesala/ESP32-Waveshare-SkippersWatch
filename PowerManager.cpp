#include "PowerManager.h"
#include "PmuRails.h"
#include "SystemStats.h"
#include "ESPNowReceiver.h"
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_task_wdt.h>

// === P U B L I C ===

PowerManager::PowerManager(PanelBrightness& brightness)
    : _brightness(brightness) {}

// Release the pin holds latched before the previous deep sleep so the panel/touch reset
// lines and the amp enable are controllable again. Called FIRST in app.begin(), before
// initDisplay() / touch begin() — a held reset line would block their reset pulse.
void PowerManager::releaseHolds() {
    gpio_hold_dis((gpio_num_t)PIN_LCD_RST);
    gpio_hold_dis((gpio_num_t)PIN_TP_RST);
    gpio_hold_dis((gpio_num_t)PIN_PA_CTRL);
    // Pads latched by definePads() (harmless when that was off): the panel bus has to be
    // free again before initDisplay() builds the QSPI bus on it.
    gpio_hold_dis((gpio_num_t)PIN_LCD_CS);
    gpio_hold_dis((gpio_num_t)PIN_LCD_SCK);
    for (uint8_t pin : PIN_LCD_SIO) gpio_hold_dis((gpio_num_t)pin);
    gpio_hold_dis((gpio_num_t)PIN_I2S_MCLK);
    gpio_deep_sleep_hold_dis();

    // Hand the wake button back to the normal GPIO matrix: after an ext0 wake the pad is
    // still in RTC mode, where digitalRead() (used by the stuck-button guard) is not
    // meaningful.
    rtc_gpio_deinit((gpio_num_t)WAKE_BUTTON_PIN);
}

// Timer wake = the stuck-button guard rechecking, never a user request. Do the absolute
// minimum: read the button, sleep again. No rails, no panel, no UI, no radio — the whole
// visit is a few hundred ms at boot current, and armWakeSource() below decides whether
// the next sleep is armed on ext0 (released) or another timer (still held).
bool PowerManager::handleTimerRecheck(esp_sleep_wakeup_cause_t cause) {
    if (cause != ESP_SLEEP_WAKEUP_TIMER) return false;

    pinMode(WAKE_BUTTON_PIN, INPUT_PULLDOWN);
    delay(5);   // let the pull settle before judging the level
    Serial.printf("[power] timer recheck: wake button %s\n",
                  (digitalRead(WAKE_BUTTON_PIN) == HIGH) ? "still held" : "released");

    this->enterDeepSleep(false);   // panel never initialised on this path — no ramp
    return true;                   // unreachable: enterDeepSleep() does not return
}

void PowerManager::begin(esp_sleep_wakeup_cause_t cause) {
    _cause = cause;

    switch (cause) {
        case ESP_SLEEP_WAKEUP_EXT0:
        case ESP_SLEEP_WAKEUP_EXT1:  Serial.println("[power] wake: button");   break;
        case ESP_SLEEP_WAKEUP_TIMER: Serial.println("[power] wake: timer");    break;
        default:                     Serial.println("[power] cold boot");      break;
    }

    _state = PowerState::DISPLAY_HOLD;
    _last_activity = millis();
    _wake_ms = _last_activity;      // the awake ceiling counts from here, and only here
    _was_charging = this->chargingNow();

    // Armed only now, at the END of the wake sequence: the display init, the 200 ms
    // dark ESP-NOW listen window and the brightness ramp all run before this, so none of
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
    // stuck input source cannot hold the board at active current until the cell is flat. This
    // check sits ABOVE the prevent-sleep branch on purpose — the switch suspends the
    // display timeout, never the ceiling, so an awake session is still bounded at 120 s.
    if ((long)(now - _wake_ms) >= (long)MAX_AWAKE_MS) {
        Serial.println("[power] awake ceiling reached -> sleep");
        _state = PowerState::SLEEP_PREP;
        this->enterDeepSleep();   // does not return
    }

    // Prevent-sleep: behave like the docked case for the rest of this session — no
    // display timeout, panel dimmed instead (brightnessCeiling()). The ceiling above
    // has already been honoured, so this cannot hold the watch awake indefinitely.
    if (this->preventSleepActive()) return;

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

// Display-on window: the user's wake time, capped by the low-charge ladder (the CrowPanel's
// main lever; on the AMOLED the balance between timeout and brightness is still [M]).
uint32_t PowerManager::displayTimeoutMs() const {
    return BatteryLadder::displayTimeoutMs(this->levelNow(), _user_timeout_ms);
}

// Two clamps can apply at once, and the LOWER always wins:
//   - the low-charge ladder (battery only; USB power means there is nothing to ration)
//   - the idle dim, in either stay-awake mode (docked on USB, or the prevent-sleep switch)
// Any touch activity calls notifyActivity(), so the dim lifts on the next loop.
int8_t PowerManager::brightnessCeiling() const {
    const bool charging = this->chargingNow();

    int8_t ceiling = charging ? CEILING_NONE : backlightCeilingFor(this->levelNow());

    if (charging || this->preventSleepActive()) {
        const bool idle = (long)(millis() - _last_activity) >= (long)STAYAWAKE_DIM_IDLE_MS;
        if (idle && STAYAWAKE_DIM_PERCENT < ceiling) ceiling = STAYAWAKE_DIM_PERCENT;
    }
    return ceiling;
}

int8_t PowerManager::backlightCeilingFor(BatteryMonitor::Level level) {
    switch (level) {
        case BatteryMonitor::Level::CUTOFF:   return CEILING_CUTOFF;
        case BatteryMonitor::Level::CRITICAL: return CEILING_CRITICAL;
        default:                              return CEILING_NONE;
    }
}

// === P R I V A T E ===

// SLEEP_PREP → deep sleep. Order: shed loads, sleep peripherals, latch pins, arm wake, sleep.
void PowerManager::enterDeepSleep(bool ramp_brightness) {
    Serial.println("[power] entering deep sleep");
    Serial.flush();

    // Status-screen totals. RTC RAM only, no flash, so it is safe on every path — the
    // stuck-button recheck included, where it adds that visit's awake time too.
    SystemStats::onSleepEntry(ESPNowReceiver::radioFrameCount());

    // Pre-sleep flush FIRST, while the radio is idle and no render is in flight: a flash
    // (NVS) write here is the only current spike in that instant. Null on the stuck-button
    // recheck path, which deliberately touches nothing.
    if (_sleep_hook != nullptr) _sleep_hook(_sleep_ctx);

    // Unsubscribe before the ramp so a shortened WDT timeout could never trip here
    if (_wdt_armed) {
        esp_task_wdt_delete(NULL);
        _wdt_armed = false;
    }

    if (ramp_brightness) this->rampBrightnessDown();  // soft-shed the panel first
    this->gatePeripherals();     // panel + touch asleep, reset/amp lines latched
    this->armWakeSource();       // PWR button (GPIO10), or a timer if it is held

    esp_deep_sleep_start();       // full power-down; next wake re-runs setup()
}

// Brightness soft ramp-down to 0 (never a hard step off)
void PowerManager::rampBrightnessDown() {
    uint32_t level = _brightness.current();
    for (uint8_t i = 0; i < RAMP_STEPS; ++i) {
        level = (level > (255 / RAMP_STEPS)) ? (level - (255 / RAMP_STEPS)) : 0;
        _brightness.set((uint8_t)level);
        delay(RAMP_STEP_MS);
    }
    _brightness.set(0);
}

// Put the panel (CO5300 display-off + sleep-in) and the touch controller (hibernate) to
// sleep, switch the AXP2101 rails to their sleep state (the AMOLED module's own supply
// off), keep the speaker amp off, then latch the lines so nothing floats through deep
// sleep. Rail order matters: the panel is asleep before its supply enable drops.
void PowerManager::gatePeripherals() {
    if (_panel != nullptr) {
        _brightness.set(0);
        _panel->displayOff();
    }
    if (_touch != nullptr) _touch->hibernate();
    if (_rails != nullptr) _rails->enterSleep();   // last I2C traffic of this wake
    if (SLEEP_DEFINE_PADS) this->definePads();

    pinMode(PIN_LCD_RST, OUTPUT);
    digitalWrite(PIN_LCD_RST, HIGH);   // not in reset: the panel stays in sleep-in
    pinMode(PIN_TP_RST, OUTPUT);
    digitalWrite(PIN_TP_RST, HIGH);    // not in reset: the touch stays in hibernate
    pinMode(PIN_PA_CTRL, OUTPUT);
    digitalWrite(PIN_PA_CTRL, LOW);    // speaker amp off

    gpio_hold_en((gpio_num_t)PIN_LCD_RST);
    gpio_hold_en((gpio_num_t)PIN_TP_RST);
    gpio_hold_en((gpio_num_t)PIN_PA_CTRL);
    gpio_deep_sleep_hold_en();
}

// Give every panel and I2S pad a defined level through deep sleep. Both the panel's
// VDDIO and the ES8311's digital supply stay on VCC3V3, so a floating pad there is a
// powered CMOS input left halfway — a classic source of a few hundred µA each. The
// QSPI bus driver is not torn down first: the pins are simply reclaimed as GPIOs, and
// the next boot builds the bus from scratch after releaseHolds().
void PowerManager::definePads() {
    pinMode(PIN_LCD_CS, OUTPUT);
    digitalWrite(PIN_LCD_CS, HIGH);    // panel interface deselected
    gpio_hold_en((gpio_num_t)PIN_LCD_CS);
    pinMode(PIN_LCD_SCK, OUTPUT);
    digitalWrite(PIN_LCD_SCK, LOW);
    gpio_hold_en((gpio_num_t)PIN_LCD_SCK);
    for (uint8_t pin : PIN_LCD_SIO) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
        gpio_hold_en((gpio_num_t)pin);
    }

    pinMode(PIN_I2S_MCLK, OUTPUT);
    digitalWrite(PIN_I2S_MCLK, LOW);
    gpio_hold_en((gpio_num_t)PIN_I2S_MCLK);
    for (uint8_t pin : PIN_I2S_OUT) {     // digital pads: gpio_deep_sleep_hold_en() latches
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
    pinMode(PIN_I2S_ASDOUT, INPUT_PULLDOWN);
}

// Arm the PWR button (SYS_OUT, ACTIVE-HIGH) as an ext1 (or ext0) deep-sleep wake source.
//
// ext0 and ext1 are LEVEL-triggered: arming it while the button is already down (strap or sleeve
// pressure) makes esp_deep_sleep_start() return immediately — a reboot loop at active
// current that would empty the cell in a few hours. So if the button is held, arm a
// timer instead and let handleTimerRecheck() look again in 30 s without lighting the
// watch up. Each recheck is a few hundred ms at boot current.
void PowerManager::armWakeSource() {
    // Wake sources are cumulative within one boot — clear before arming so a timer
    // recheck can never leave a stale timer source alongside a freshly armed ext0.
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    pinMode(WAKE_BUTTON_PIN, INPUT_PULLDOWN);
    delayMicroseconds(200);

    if (digitalRead(WAKE_BUTTON_PIN) == HIGH) {
        Serial.println("[power] arm: wake button held -> timer recheck in 30 s");
        Serial.flush();
        esp_sleep_enable_timer_wakeup(STUCK_BUTTON_RECHECK_US);
        return;
    }

    if (WAKE_ON_EXT1) {
        esp_sleep_disable_ext1_wakeup_io(0);   // drop any earlier ext1 pin set
        // RTC-controller wake: works with the RTC_PERIPH domain powered down, which ext0
        // would keep on. No internal pull (it would not survive that domain anyway):
        // T1 holds SYS_OUT low at rest and R11 pulls it high on a press.
        esp_sleep_enable_ext1_wakeup_io(1ULL << WAKE_BUTTON_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);
        return;
    }

    rtc_gpio_init((gpio_num_t)WAKE_BUTTON_PIN);
    rtc_gpio_set_direction((gpio_num_t)WAKE_BUTTON_PIN, RTC_GPIO_MODE_INPUT_ONLY);
    // SYS_OUT is driven by the board's power-key circuit; the pull-down only keeps the
    // pad defined if it is not [M: check the level stays low at rest in deep sleep].
    rtc_gpio_pullup_dis((gpio_num_t)WAKE_BUTTON_PIN);
    rtc_gpio_pulldown_en((gpio_num_t)WAKE_BUTTON_PIN);   // released = low, press = high
    esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_BUTTON_PIN, 1);  // wake when high (pressed)
}

// Subscribe the Arduino loop task to the task watchdog. The awake ceiling only fires
// while loop() still runs; this covers the case where it does not (a blocking bug in
// LVGL, I2C or a page adapter). A trip resets the SoC → cold boot → normal 10 s window,
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
