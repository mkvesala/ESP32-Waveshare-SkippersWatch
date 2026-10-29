#include "WatchApplication.h"
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>

// File-scope display state — shared with the static LVGL flush callback (runs outside
// the class instance). Mirrors the vendor example's global display objects, kept local
// to this translation unit.
static Arduino_DataBus* s_bus = nullptr;
static Arduino_CO5300*  s_gfx = nullptr;

// Last-active page index, tier 1 of two (tier 2 is NVS — see resolveStartPage()).
//
// .rtc.noinit, NOT .rtc.data: the bootloader reloads .rtc.data from the flash image on
// every boot EXCEPT a deep-sleep wake, where it deliberately skips the initialisation.
// A RTC_DATA_ATTR value therefore survives sleep but is wiped by a reset, a watchdog
// trip or a panic — which is exactly the case this tier has to cover.
// .rtc.noinit is not in the image at all, so nothing ever initialises it.
//
// It is genuinely uninitialised after a power-on (and after any reset that power-cycles
// the RTC domain — the AXP2101 long-press power-off certainly does), so the value is
// only trusted when the magic word matches. ScreenManager::begin() clamps regardless.
RTC_NOINIT_ATTR uint32_t s_page_magic;
RTC_NOINIT_ATTR uint8_t  s_last_page_index;
static constexpr uint32_t PAGE_MAGIC = 0x5057A6E1;

// LVGL flush: blit the dirty area to the CO5300 over QSPI. No tearing-effect sync in
// this phase: the panel only drives TE after a TEON command, and waiting on a silent
// TE line would stall every flush.
static void lvglFlushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    const uint32_t w = area->x2 - area->x1 + 1;
    const uint32_t h = area->y2 - area->y1 + 1;
    // NOTE: if colors appear byte-swapped, add lv_draw_sw_rgb565_swap(px_map, w*h) here.
    s_gfx->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t*>(px_map), w, h);
    lv_display_flush_ready(disp);
}

// The CO5300 wants column/row windows that start on an even and end on an odd
// coordinate; partial flushes without this produce artefacts. As in Waveshare's LVGL
// examples: round every invalidated area out to that grid.
static void lvglRounderCb(lv_event_t* e) {
    lv_area_t* area = static_cast<lv_area_t*>(lv_event_get_param(e));
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

// === P U B L I C ===

WatchApplication::WatchApplication()
    : _demo(_receiver)
    , _navUI(_receiver)
    , _batteryUI(_receiver)
    , _weatherUI(_receiver)
    , _engineUI(_receiver)
    , _attitudeUI(_receiver)
    , _celestialUI(_receiver, _lastPosition)
    , _setupUI(_brightness)
    , _power(_brightness)
    , _theme(_battery, _menu)
    , _status(_stats, _battery) {}

void WatchApplication::begin() {
    // Read the wake cause once, up front: it decides both the stuck-button path and, at
    // the end, _power.begin().
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    // FIRST: release the GPIO holds from the previous deep sleep (panel/touch reset lines,
    // amp enable), so the reset pulses below actually reach the chips.
    _power.releaseHolds();

    // Status-screen counters, before the stuck-button recheck (which may sleep right
    // away): a timer recheck is not counted as a wake-up, but its awake time is.
    _stats.onBoot(cause, esp_reset_reason());

    // Speaker amp off from the first instant: audio is never used by the watch.
    pinMode(PIN_PA_CTRL, OUTPUT);
    digitalWrite(PIN_PA_CTRL, LOW);

    // Stuck wake-button guard: a timer wake is never a user request, only the guard
    // rechecking a held button. Sleep straight back without touching panel, UI or radio.
    // Does not actually return — the return is for the compiler's benefit.
    if (_power.handleTimerRecheck(cause)) return;

    // Timezone for the Clock page: Finland (EET=UTC+2, EEST=UTC+3, DST last-Sun-Mar
    // 03:00 → last-Sun-Oct 04:00). Set once so localtime_r renders local time with DST;
    // the system clock itself is UTC, disciplined by GNSS DATETIME_DELTA (handleTimeSync).
    setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
    tzset();

    // Shared I2C bus (touch + PMIC). The PMIC comes up first: the battery level decides
    // below whether the radio is brought up at all.
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    _battery.beginPmu(Wire, PIN_I2C_SDA, PIN_I2C_SCL);

    this->initDisplay();     // panel initialised DARK (brightness 0)
    this->initLvgl();
    _power.attachPeripherals(s_gfx, &_touch);

    // SquareLine UI (creates ui_ScreenMain + all containers, loads the screen)
    ui_init();

    // Bring up each page adapter (SetupUI applies persisted brightness here)
    _navUI.begin();
    _batteryUI.begin();
    _weatherUI.begin();
    _engineUI.begin();
    _attitudeUI.begin();
    _clockUI.begin();
    _setupUI.begin();
    _celestialUI.begin();
    _menu.begin();           // hides ui_ContainerMenu (the export leaves it visible)
    _status.begin();         // hides ui_ContainerStatus (likewise) and writes its titles

    // Watch's own battery gauge on the always-visible top bar. Safe here: the PMIC is up
    // and ui_BarBatteryIndicator exists (ui_init). Seeds the bar with a real value now.
    _battery.begin();

    lv_label_set_text(ui_LabelVersion, SW_VERSION);

    // Palette and data source from the stored Setup choices, BEFORE the first frame is
    // rendered: a night wake is red from its first lit frame, and in demo mode the
    // receiver already holds demo values when the page is revealed.
    _theme.begin();
    this->applyModes();

    // Low-charge cutoff BEFORE the radio comes up: RF bring-up is the sharpest current
    // edge on the board and the last thing a near-empty cell should be asked for. Shows
    // the charge prompt and sleeps; never returns when it triggers.
    this->checkBatteryCutoff();

    // Register the carousel of container-backed pages, in order (MenuUI::Page matches)
    _screenMgr.addScreen(&_navUI);
    _screenMgr.addScreen(&_batteryUI);
    _screenMgr.addScreen(&_weatherUI);
    _screenMgr.addScreen(&_engineUI);
    _screenMgr.addScreen(&_attitudeUI);
    _screenMgr.addScreen(&_clockUI);
    _screenMgr.addScreen(&_setupUI);
    _screenMgr.addScreen(&_celestialUI);
    // Restore the last-active page from RTC, else NVS, else Navigation. The actual
    // _screenMgr.begin(start_page) is deferred to after the dark listen window below, so
    // the page is revealed only once data has had a chance to arrive.
    const uint8_t start_page = this->resolveStartPage();

    // A true power-on (power was cut, e.g. PWR held ~6 s) gets the startup screen; a deep-
    // sleep wake goes straight to the page. RTC-domain resets other than POWERON (watchdog,
    // panic, software) also go straight to the page.
    const bool power_on = (esp_reset_reason() == ESP_RST_POWERON);

    // --- Staggered wake sequence (runs on every wake, cold boot included) ---
    // Panel is initialised DARK above and stays dark through the listen window
    // below. Bring the radio up FIRST — into that dark, low-load moment — then
    // hold black while it catches a broadcast, THEN reveal the page with data
    // already in hand, THEN ramp the brightness. The radio inrush and the
    // panel light-up are separated in time.
    _touch.begin();                   // capacitive touch (shared I2C) — polled while awake
    _receiver.begin(ESP_NOW_CHANNEL); // radio up first, panel still dark

    if (power_on) {
        // The startup screen is lit (with the normal soft ramp) while the radio listens,
        // so it replaces the dark listen window; the page then appears at full brightness.
        this->showStartupScreen();
    } else {
        // Dark ESP-NOW listen window: delay() yields to FreeRTOS so the receiver's
        // RX callbacks can land a fresh packet before anything is shown. No LVGL
        // render happens here — the panel stays black until lv_refr_now() below.
        delay(ESPNOW_WAKE_LISTEN_MS);
    }

    // Apply any GNSS datetime caught during the listen window BEFORE the first lit
    // frame, so the Clock page (if restored) shows the synced time immediately.
    this->handleTimeSync();

    // Demo mode: a fresh set right before the reveal, so the first frame carries current
    // values even after the 3 s startup screen.
    if (_demo_active) _demo.sendAll(millis());

    // Reveal the last-active page (onEnter() sets "---"), then immediately
    // update() so any packet caught during the window repaints real values
    // BEFORE the first lit frame — the placeholder is only seen if nothing
    // arrived (correct: never fabricate data).
    _screenMgr.begin(start_page);
    IScreenUI* screen = _screenMgr.getCurrentScreen();
    if (screen) screen->update();

    lv_refr_now(_lvgl_disp);          // render the first frame into the dark panel
    if (!power_on) {
        // Clamp the brightness for the current charge level BEFORE the ramp, so a low cell
        // never flashes bright and then dims. 100 % (no clamp) in the normal case.
        _setupUI.setBrightnessCeiling(PowerManager::backlightCeilingFor(_battery.level()));
        _setupUI.rampBacklightUp();   // brightness soft-start — user sees real data
    }

    // Model B power owner — records wake cause, starts the display-on timer + the
    // absolute awake ceiling, and arms the task watchdog. The battery is attached first:
    // the low-charge ladder and the USB stay-awake rule both read it.
    _power.attachBattery(&_battery);
    _power.begin(cause);

    // Registered LAST, on purpose: the stuck-wake-button recheck sleeps from the very top
    // of this function, long before here, so that path still sees a null hook and writes
    // no flash. Every real sleep (timeout, awake ceiling, protective) goes through it.
    _power.setPreSleepHook(&WatchApplication::preSleepFlush, this);
}

void WatchApplication::loop() {
    const uint32_t loop_start_us = micros();
    const uint32_t now = millis();
    this->handleLvglTick(now);
    this->handleTouch(now);
    this->handleTimeSync();
    this->handleUIUpdate(now);
    _battery.update(now);   // refresh the top battery bar (self-gated, ~3 s cadence)
    this->checkBatteryCutoff();   // near-empty cell → charge prompt + protective sleep

    // Demo / night switches → data source and palette (no-op unless one flipped)
    this->applyModes();
    if (_demo_active) _demo.update(now);

    // User's prevent-sleep choice and wake time → power policy. PowerManager decides
    // whether they may be honoured as such (the low-charge ladder overrides both).
    _power.setPreventSleep(_setupUI.preventSleep());
    _power.setDisplayTimeout(_setupUI.waketimeMs());

    // Backlight clamp for the current power state: low-charge ladder on battery, idle
    // dim in either stay-awake mode. No-ops unless the ceiling actually moved.
    _setupUI.setBrightnessCeiling(_power.brightnessCeiling());

    // Loop runtime for the Status screen, measured before _power.loop(), which may sleep
    _stats.onLoop(micros() - loop_start_us);

    _power.loop(now);   // may enter deep sleep after the display-on timeout (no return)
}

// === P R I V A T E ===

// Which page to open on, from the two-tier store. Tier 1 (RTC .rtc.noinit) is free and
// always current; tier 2 (NVS) is only read when tier 1 is gone, i.e. after a power loss
// or a reset that power-cycled the RTC domain. On the common deep-sleep wake this touches
// no flash at all, so the wake sequence is unchanged.
//
// The log line is also the measurement: comparing the reset reason against which tier
// supplied the value tells which of this board's resets / power-offs wipe RTC memory.
uint8_t WatchApplication::resolveStartPage() {
    const esp_reset_reason_t reason = esp_reset_reason();

    uint8_t page;
    const char* src;
    if (s_page_magic == PAGE_MAGIC) {
        page = s_last_page_index;
        src  = "rtc";
    } else {
        page = this->loadPageFromNvs();
        src  = "nvs";
        // Repair tier 1 so the next sleep cycle is back on the free path
        s_page_magic      = PAGE_MAGIC;
        s_last_page_index = page;
    }

    // Seed the write-elision baseline: NVS already holds this on the "nvs" path, and on
    // the "rtc" path it holds whatever the last pre-sleep flush wrote — which is this
    // same value, because that flush is the only writer.
    _nvs_page_written = page;

    // ...except when we did NOT arrive from deep sleep. Then the tiers can legitimately
    // disagree: switch a page, press RESET before the display times out, and RTC carries
    // the new page while NVS still holds the one from the last sleep. Force one flush on
    // the way into the next sleep so they converge — otherwise a later battery pull would
    // silently fall back a page. Costs one write on a reset/cold boot, never on a wake.
    if (reason != ESP_RST_DEEPSLEEP) _nvs_page_written = 0xFF;

    Serial.printf("[page] reset=%d src=%s page=%u\n", (int)reason, src, page);
    return page;
}

// Tier 2 read. Returns 0 (Navigation) when the key has never been written.
uint8_t WatchApplication::loadPageFromNvs() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE_UI, true);
    uint8_t v = prefs.getUChar(NVS_KEY_LASTPAGE, 0);
    prefs.end();
    return v;
}

// Tier 2 write. Called only from the pre-sleep flush, and only when the page changed —
// a flash program is a current spike and a wear event, so it happens at most once per
// awake session instead of once per swipe.
void WatchApplication::savePageToNvs() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE_UI, false);
    prefs.putUChar(NVS_KEY_LASTPAGE, s_last_page_index);
    prefs.end();
}

// PowerManager pre-sleep hook: persist the page and Celestial's last known position to flash
// on the way down. Runs before the brightness ramp, with the radio idle and no render in
// flight, so these writes are the only load moving at that instant.
void WatchApplication::preSleepFlush(void* ctx) {
    WatchApplication* self = static_cast<WatchApplication*>(ctx);
    if (self == nullptr) return;
    self->_lastPosition.saveOnSleep();
    if (self->_nvs_page_written == s_last_page_index) return;   // nothing changed
    self->savePageToNvs();
    self->_nvs_page_written = s_last_page_index;
    Serial.printf("[page] persisted page=%u\n", s_last_page_index);
}

// Allocate the QSPI bus + CO5300 AMOLED and start it DARK. The vendor init sequence
// sets brightness 0xD0, so it is forced to 0 straight away (staggered wake: the panel
// lights only with the brightness ramp after the first real frame).
void WatchApplication::initDisplay() {
    s_bus = new Arduino_ESP32QSPI(PIN_LCD_CS, PIN_LCD_SCK,
                                  PIN_LCD_SIO0, PIN_LCD_SIO1, PIN_LCD_SIO2, PIN_LCD_SIO3);
    s_gfx = new Arduino_CO5300(s_bus, PIN_LCD_RST, 0 /*rotation*/,
                               SCREEN_WIDTH, SCREEN_HEIGHT,
                               LCD_COL_OFFSET, 0, 0, 0);

    if (!s_gfx->begin()) {
        Serial.println("ERROR: CO5300 initialization failed");
        while (true) delay(1000);
    }
    _brightness.attach(s_gfx);
    _brightness.set(0);
    s_gfx->fillScreen(RGB565_BLACK);
}

// Create the LVGL display with a small partial-render buffer and the CO5300 rounder
void WatchApplication::initLvgl() {
    lv_init();
    lv_tick_set_cb(millis);

    const size_t buf_bytes = (size_t)SCREEN_WIDTH * DRAW_BUFFER_LINES * sizeof(uint16_t);
    _buf1 = (uint16_t*)heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (_buf1 == nullptr) {
        Serial.println("ERROR: LVGL draw buffer allocation failed");
        while (true) delay(1000);
    }

    _lvgl_disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_color_format(_lvgl_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(_lvgl_disp, lvglFlushCb);
    lv_display_add_event_cb(_lvgl_disp, lvglRounderCb, LV_EVENT_INVALIDATE_AREA, nullptr);
    lv_display_set_buffers(_lvgl_disp, _buf1, nullptr, buf_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
}

// Adaptive LVGL tick: poll between LVGL_TICK_MIN/MAX ms, driven by lv_timer_handler()
void WatchApplication::handleLvglTick(const uint32_t now) {
    if (now - _last_lvgl_tick < _next_lvgl_interval_ms) return;
    _last_lvgl_tick = now;

    uint32_t next_ms = lv_timer_handler();
    if (next_ms < LVGL_TICK_MIN_MS)      _next_lvgl_interval_ms = LVGL_TICK_MIN_MS;
    else if (next_ms > LVGL_TICK_MAX_MS) _next_lvgl_interval_ms = LVGL_TICK_MAX_MS;
    else                                 _next_lvgl_interval_ms = next_ms;
}

// Touch: the watch's only input device. On touch-down the active page may CLAIM the
// touch as a drag (interceptsTouchAt, e.g. the Setup slider); if so we stream
// onTouchDrag/onTouchEnd to it. Otherwise the release is classified into a discrete
// gesture and handled by handleGesture(). While the page menu is open no page sees the
// touch at all.
void WatchApplication::handleTouch(const uint32_t now) {
    _touch.poll(now);

    // Touch-down: decide whether the active page wants this touch as a drag.
    if (_touch.justPressed()) {
        _power.notifyActivity();
        if (this->overlayVisible()) {
            _touch_dragging = false;
            return;
        }
        int16_t x, y;
        _touch.point(x, y);
        IScreenUI* screen = _screenMgr.getCurrentScreen();
        _touch_dragging = (screen && screen->interceptsTouchAt(x, y));
        if (_touch_dragging) screen->onTouchDrag(x, y);
        return;
    }

    // Stream a claimed drag while the finger is held.
    if (_touch_dragging && _touch.isDown()) {
        _power.notifyActivity();
        int16_t x, y;
        _touch.point(x, y);
        IScreenUI* screen = _screenMgr.getCurrentScreen();
        if (screen) screen->onTouchDrag(x, y);
        return;
    }

    // Release: finish a drag, or act on the classified gesture.
    if (_touch.justReleased()) {
        _power.notifyActivity();
        if (_touch_dragging) {
            IScreenUI* screen = _screenMgr.getCurrentScreen();
            if (screen) screen->onTouchEnd();
            _touch_dragging = false;
            _touch.getGesture();   // discard any classification for a claimed drag
            return;
        }
        this->handleGesture(_touch.getGesture());
    }
}

// Gesture → action, by whether the page menu is open:
//   page: swipe left = next page, swipe right = previous page, swipe down = open menu,
//         tap = the page's onButtonPress()
//   menu: swipe up = close menu (last-active page returns), tap on a button = that page;
//         everything else is ignored
//   status: swipe down = close status (last-active page returns); everything else ignored
//   page: swipe up = open status
void WatchApplication::handleGesture(TouchGesture gesture) {
    if (_status.isVisible()) {
        if (gesture == TouchGesture::SWIPE_DOWN) this->closeStatus();
        return;
    }

    if (_menu.isVisible()) {
        if (gesture == TouchGesture::SWIPE_UP) {
            this->closeMenu();
        } else if (gesture == TouchGesture::TAP) {
            int16_t x, y;
            _touch.point(x, y);
            const int8_t page = _menu.pageAt(x, y);
            if (page >= 0) this->jumpToPage((uint8_t)page);
        }
        return;
    }

    IScreenUI* screen = _screenMgr.getCurrentScreen();
    switch (gesture) {
        case TouchGesture::TAP:
            if (screen) screen->onButtonPress();
            break;
        case TouchGesture::SWIPE_LEFT:
            _screenMgr.switchNext();
            this->rememberPage();
            break;
        case TouchGesture::SWIPE_RIGHT:
            _screenMgr.switchPrevious();
            this->rememberPage();
            break;
        case TouchGesture::SWIPE_DOWN:
            this->openMenu();
            break;
        case TouchGesture::SWIPE_UP:
            this->openStatus();
            break;
        default:
            break;   // LONG_PRESS reserved, NONE ignored
    }
}

// Menu over the current page: the page container is hidden (the menu container is
// transparent), the page itself is not left — closing the menu brings it straight back.
void WatchApplication::openMenu() {
    _screenMgr.hideCurrent();
    _menu.show();
}

void WatchApplication::closeMenu() {
    _menu.hide();
    _screenMgr.showCurrent();
}

// Status over the current page, the same way as the menu: the page is hidden, not left.
void WatchApplication::openStatus() {
    _screenMgr.hideCurrent();
    _status.show();
}

void WatchApplication::closeStatus() {
    _status.hide();
    _screenMgr.showCurrent();
}

void WatchApplication::jumpToPage(uint8_t index) {
    _menu.hide();
    _screenMgr.switchToIndex(index);
    _screenMgr.showCurrent();   // also covers a tap on the page that was already active
    this->rememberPage();
}

// Tier 1 only — free. Flash (tier 2) is written once on the way into sleep.
void WatchApplication::rememberPage() {
    s_last_page_index = _screenMgr.currentIndex();
}

// Discipline the system clock from GNSS: apply a freshly received UTC datetime.
// Runs regardless of the active page so the Clock is correct whenever it is shown.
// Cheap when idle (one spinlock-guarded flag read); settimeofday only on a new packet.
void WatchApplication::handleTimeSync() {
    if (!_receiver.hasNewDatetimeData()) return;
    DateTimeDelta dt = _receiver.getDatetimeData();
    if (!dt.time_valid) return;
    // The sender truncates to whole seconds: +0.5 s centres the error (±0.5 s instead of
    // 0..1 s late), which the Celestial page's Hc relies on (1 s ≈ 0.25' at most)
    struct timeval tv = { (time_t)dt.unix_utc, 500000 };
    settimeofday(&tv, nullptr);
}

void WatchApplication::applyModes() {
    const bool demo = _setupUI.demoMode();
    if (demo != _demo_active) {
        _demo_active = demo;
        _receiver.setDemoMode(demo);   // clears every source both ways
        _battery.setDemoSuffix(demo);
        _clockUI.setDemo(demo);
        _celestialUI.setDemo(demo);
        _lastPosition.setDemo(demo);
        if (demo) _demo.sendAll(millis());   // no placeholder gap after switching on
        Serial.printf("[demo] %s\n", demo ? "ON (ESP-NOW data not shown)" : "OFF");
    }
    _theme.apply(_setupUI.nightMode());
}

// Lit startup screen during a power-on wake. Runs before PowerManager::begin(), so the
// watchdog is not armed yet and the display-on window has not started; touch is not
// polled, so nothing can act on a tap here.
void WatchApplication::showStartupScreen() {
    _screenMgr.hideAll();   // the export may leave a page visible (it does: Setup)
    lv_obj_add_flag(ui_ContainerStatus, LV_OBJ_FLAG_HIDDEN);   // and Status
    lv_obj_remove_flag(ui_ContainerStartup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui_ContainerStartup);
    lv_refr_now(_lvgl_disp);

    _setupUI.setBrightnessCeiling(PowerManager::backlightCeilingFor(_battery.level()));
    _setupUI.rampBacklightUp();

    const uint32_t start = millis();
    while (millis() - start < STARTUP_SCREEN_MS) {
        lv_timer_handler();
        this->handleTimeSync();
        delay(10);
    }
    lv_obj_add_flag(ui_ContainerStartup, LV_OBJ_FLAG_HIDDEN);
}

// Periodic update of the active page only (~17 Hz). Skipped while an overlay covers it:
// the container is hidden, and the next update after closing repaints fresh values. The
// Status overlay refreshes itself (1 Hz) instead.
void WatchApplication::handleUIUpdate(const uint32_t now) {
    if (now - _last_ui_update < UI_UPDATE_INTERVAL_MS) return;
    _last_ui_update = now;
    // Celestial's last known position follows the live fix on every page, overlays included
    _lastPosition.observe(_receiver.getGnssData(), _receiver.lastGnssRxMillis(), now);
    if (_status.isVisible()) { _status.update(now); return; }
    if (_menu.isVisible()) return;

    IScreenUI* screen = _screenMgr.getCurrentScreen();
    if (screen) screen->update();
}

// Near-empty cell: show an unmistakable prompt, then power down before the cell's own
// protection circuit does it for us. The button wake stays armed, so a press just
// repeats this short cycle and plugging in USB restores a normal boot.
void WatchApplication::checkBatteryCutoff() {
    if (_battery.charging()) return;
    if (_battery.level() != BatteryMonitor::Level::CUTOFF) return;

    this->showBatteryCutoffScreen();
    _power.enterProtectiveSleep();   // does not return
}

// Code-built overlay (no SquareLine export involved): an opaque full-screen panel on top
// of whatever page is loaded, so it works during the wake sequence too — before the page
// adapters have been revealed. Lit at the CUTOFF brightness ceiling only.
void WatchApplication::showBatteryCutoffScreen() {
    lv_obj_t* screen = lv_screen_active();
    if (screen != nullptr) {
        lv_obj_t* overlay = lv_obj_create(screen);
        lv_obj_remove_style_all(overlay);
        lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_center(overlay);
        lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* label = lv_label_create(overlay);
        lv_label_set_text(label, "BATTERY EMPTY\n\nCHARGE NOW");
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(label, lv_color_hex(0xF44336), LV_PART_MAIN);
        lv_obj_set_style_text_font(label, &ui_font_FontLabel14, LV_PART_MAIN);
        lv_obj_center(label);

        lv_refr_now(_lvgl_disp);
    }

    // Clamp to the CUTOFF ceiling (applied immediately by SetupUI). If the panel is
    // still dark — the cutoff was caught during the wake sequence, before the normal
    // ramp — soft-start it so the prompt is actually visible.
    _setupUI.setBrightnessCeiling(PowerManager::backlightCeilingFor(BatteryMonitor::Level::CUTOFF));
    if (_brightness.current() == 0) _setupUI.rampBacklightUp();
    delay(CUTOFF_MESSAGE_MS);
}
