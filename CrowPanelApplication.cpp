#include "CrowPanelApplication.h"
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>

// File-scope display state — shared with the static LVGL flush callback and the
// tearing-effect ISR (both run outside the class instance). Mirrors the Elecrow
// example's global display objects, kept local to this translation unit.
static Arduino_DataBus*   s_bus = nullptr;
static Arduino_GFX*       s_gfx = nullptr;
static SemaphoreHandle_t  s_te_sem = nullptr;

// Last-active page index, tier 1 of two (tier 2 is NVS — see resolveStartPage()).
//
// .rtc.noinit, NOT .rtc.data: the bootloader reloads .rtc.data from the flash image on
// every boot EXCEPT a deep-sleep wake, where it deliberately skips the initialisation.
// A RTC_DATA_ATTR value therefore survives sleep but is wiped by a reset button press,
// a watchdog trip or a panic — which is exactly the case this tier has to cover.
// .rtc.noinit is not in the image at all, so nothing ever initialises it.
//
// It is genuinely uninitialised after a power-on (and after any reset that power-cycles
// the RTC domain — an EN-pin reset may or may not, board-dependent), so the value is
// only trusted when the magic word matches. ScreenManager::begin() clamps regardless.
RTC_NOINIT_ATTR uint32_t s_page_magic;
RTC_NOINIT_ATTR uint8_t  s_last_page_index;
static constexpr uint32_t PAGE_MAGIC = 0x5057A6E1;

// Tearing-effect interrupt (LCD_TE, GPIO21, FALLING) — signals a safe refresh moment
static void IRAM_ATTR onTearingEffect() {
    BaseType_t hp_task_woken = pdFALSE;
    if (s_te_sem) xSemaphoreGiveFromISR(s_te_sem, &hp_task_woken);
    if (hp_task_woken) portYIELD_FROM_ISR();
}

// LVGL flush: wait for a TE event, then blit the dirty area to the GC9309
static void lvglFlushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    const uint32_t w = area->x2 - area->x1 + 1;
    const uint32_t h = area->y2 - area->y1 + 1;

    if (s_te_sem) {
        xSemaphoreTake(s_te_sem, 0);                    // clear any stale TE event
        xSemaphoreTake(s_te_sem, pdMS_TO_TICKS(100));   // wait for a fresh refresh moment
    }
    // NOTE: if colors appear byte-swapped, add lv_draw_sw_rgb565_swap(px_map, w*h) here.
    s_gfx->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t*>(px_map), w, h);
    lv_display_flush_ready(disp);
}

// === P U B L I C ===

CrowPanelApplication::CrowPanelApplication()
    : _navUI(_receiver)
    , _batteryUI(_receiver)
    , _weatherUI(_receiver)
    , _engineUI(_receiver)
    , _attitudeUI(_receiver)
    , _setupUI(PIN_BACKLIGHT)
    , _encoder()
    , _power(PIN_BACKLIGHT, PIN_LCD_POWER, PIN_PERIPH_POWER) {}

void CrowPanelApplication::begin() {
    // Read the wake cause once, up front: it decides both the start page (restore the
    // last-active page only on a real button-wake) and, at the end, _power.begin().
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    // FIRST: release any GPIO holds from the previous deep sleep, so the rail/backlight
    // writes below actually take effect (otherwise the panel wakes to a black screen).
    _power.releaseHolds();

    // Stuck wake-button guard: a timer wake is never a user request, only the guard
    // rechecking a held button. Sleep straight back without touching rails, panel, UI
    // or radio. Does not actually return — the return is for the compiler's benefit.
    if (_power.handleTimerRecheck(cause)) return;

    // Timezone for the Clock page: Finland (EET=UTC+2, EEST=UTC+3, DST last-Sun-Mar
    // 03:00 → last-Sun-Oct 04:00). Set once so localtime_r renders local time with DST;
    // the system clock itself is UTC, disciplined by GNSS DATETIME_DELTA (handleTimeSync).
    setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
    tzset();

    this->initPowerRails();
    this->initBacklight();
    this->initDisplay();
    this->initLvgl();

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

    // Watch's own battery gauge on the always-visible top bar. Safe here: the
    // peripheral rail (GPIO47) powering the divider is up (initPowerRails) and
    // ui_BarBatteryIndicator exists (ui_init). Seeds the bar with a real value now.
    _battery.begin();

    // Low-charge cutoff BEFORE the radio comes up: RF bring-up is the sharpest current
    // edge on the board and the last thing a near-empty cell should be asked for. Shows
    // the charge prompt and sleeps; never returns when it triggers.
    this->checkBatteryCutoff();

    // Register the carousel of container-backed pages, in order
    _screenMgr.addScreen(&_navUI);
    _screenMgr.addScreen(&_batteryUI);
    _screenMgr.addScreen(&_weatherUI);
    _screenMgr.addScreen(&_engineUI);
    _screenMgr.addScreen(&_attitudeUI);
    _screenMgr.addScreen(&_clockUI);
    _screenMgr.addScreen(&_setupUI);
    // Restore the last-active page from RTC, else NVS, else Navigation. Deliberately NOT
    // gated on the wake cause any more: the cause answers "how did we boot", while the
    // real question is "is the stored value trustworthy" — which the magic word answers,
    // and which a reset button press must also pass.
    // The actual _screenMgr.begin(start_page) is deferred to after the dark listen window
    // below, so the page is revealed only once data has had a chance to arrive.
    const uint8_t start_page = this->resolveStartPage();

    // --- Staggered wake sequence (runs on every wake, cold boot included) ---
    // Panel is initialised DARK above and stays dark through the listen window
    // below. Bring the radio up FIRST — into that dark, low-load moment — then
    // hold black while it catches a broadcast, THEN reveal the page with data
    // already in hand, THEN ramp the backlight. The radio inrush and the
    // backlight step are separated in time, so no single instant sees them
    // stacked (better peak separation than the old radio-last order).
    _encoder.begin();
    _touch.begin();                   // capacitive touch (shared I2C) — polled while awake
    _receiver.begin(ESP_NOW_CHANNEL); // radio up first, panel still dark

    // Dark ESP-NOW listen window: delay() yields to FreeRTOS so the receiver's
    // RX callbacks can land a fresh packet before anything is shown. No LVGL
    // render happens here — the panel stays black until lv_refr_now() below.
    delay(ESPNOW_WAKE_LISTEN_MS);

    // Apply any GNSS datetime caught during the listen window BEFORE the first lit
    // frame, so the Clock page (if restored) shows the synced time immediately.
    this->handleTimeSync();

    // Reveal the last-active page (onEnter() sets "---"), then immediately
    // update() so any packet caught during the window repaints real values
    // BEFORE the first lit frame — the placeholder is only seen if nothing
    // arrived (correct: never fabricate data).
    _screenMgr.begin(start_page);
    IScreenUI* screen = _screenMgr.getCurrentScreen();
    if (screen) screen->update();

    lv_refr_now(_lvgl_disp);          // render the first frame into the dark panel
    // Clamp the backlight for the current charge level BEFORE the ramp, so a low cell
    // never flashes bright and then dims. 100 % (no clamp) in the normal case.
    _setupUI.setBrightnessCeiling(PowerManager::backlightCeilingFor(_battery.level()));
    _setupUI.rampBacklightUp();       // backlight PWM soft-start — user sees real data

    // Model B power owner — records wake cause, starts the display-on timer + the
    // absolute awake ceiling, and arms the task watchdog. The battery is attached first:
    // the low-charge ladder and the USB stay-awake rule both read it.
    _power.attachBattery(&_battery);
    _power.begin(cause);

    // Registered LAST, on purpose: the stuck-wake-button recheck sleeps from the very top
    // of this function, long before here, so that path still sees a null hook and writes
    // no flash. Every real sleep (timeout, awake ceiling, protective) goes through it.
    _power.setPreSleepHook(&CrowPanelApplication::preSleepFlush, this);
}

void CrowPanelApplication::loop() {
    const uint32_t now = millis();
    this->handleLvglTick(now);
    this->handleKnobRotation();
    this->handleKnobButtonPress();
    this->handleTouch(now);
    this->handleTimeSync();
    this->handleUIUpdate(now);
    _battery.update(now);   // refresh the top battery bar (self-gated, ~3 s cadence)
    this->checkBatteryCutoff();   // near-empty cell → charge prompt + protective sleep

    // Backlight clamp for the current power state: low-charge ladder on battery, idle
    // dim while docked on USB. No-ops unless the ceiling actually moved.
    _setupUI.setBrightnessCeiling(_power.brightnessCeiling());

    _power.loop(now);   // may enter deep sleep after the display-on timeout (no return)
}

// === P R I V A T E ===

// Which page to open on, from the two-tier store. Tier 1 (RTC .rtc.noinit) is free and
// always current; tier 2 (NVS) is only read when tier 1 is gone, i.e. after a power loss
// or a reset that power-cycled the RTC domain. On the common deep-sleep wake this touches
// no flash at all, so the wake sequence is unchanged.
//
// The log line is also the measurement: comparing the reset reason against which tier
// supplied the value is what tells us whether an EN-pin reset wipes RTC on this board.
uint8_t CrowPanelApplication::resolveStartPage() {
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
uint8_t CrowPanelApplication::loadPageFromNvs() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE_UI, true);
    uint8_t v = prefs.getUChar(NVS_KEY_LASTPAGE, 0);
    prefs.end();
    return v;
}

// Tier 2 write. Called only from the pre-sleep flush, and only when the page changed —
// a flash program is a current spike and a wear event, so it happens at most once per
// awake session instead of once per swipe.
void CrowPanelApplication::savePageToNvs() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE_UI, false);
    prefs.putUChar(NVS_KEY_LASTPAGE, s_last_page_index);
    prefs.end();
}

// PowerManager pre-sleep hook: persist the page to flash on the way down. Runs before the
// backlight ramp, with the radio idle and no render in flight, so this write is the only
// load moving at that instant.
void CrowPanelApplication::preSleepFlush(void* ctx) {
    CrowPanelApplication* self = static_cast<CrowPanelApplication*>(ctx);
    if (self == nullptr) return;
    if (self->_nvs_page_written == s_last_page_index) return;   // nothing changed
    self->savePageToNvs();
    self->_nvs_page_written = s_last_page_index;
    Serial.printf("[page] persisted page=%u\n", s_last_page_index);
}

// Enable the shared peripheral rail and the (active-low) LCD power rail
void CrowPanelApplication::initPowerRails() {
    pinMode(PIN_PERIPH_POWER, OUTPUT);
    digitalWrite(PIN_PERIPH_POWER, HIGH);
    delay(30);
    pinMode(PIN_LCD_POWER, OUTPUT);
    digitalWrite(PIN_LCD_POWER, LOW);   // active-low: LOW = LCD rail on
    delay(30);
}

// Attach the backlight to LEDC, kept off until SetupUI applies the stored brightness
void CrowPanelApplication::initBacklight() {
    ledcAttach(PIN_BACKLIGHT, PWM_FREQ, PWM_RESOLUTION);
    ledcWrite(PIN_BACKLIGHT, 0);
}

// Allocate the SPI DMA bus + GC9309 panel and start it (with TE sync)
void CrowPanelApplication::initDisplay() {
    s_te_sem = xSemaphoreCreateBinary();
    pinMode(PIN_LCD_TE, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_LCD_TE), onTearingEffect, FALLING);

    s_bus = new Arduino_ESP32SPIDMA(PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCK,
                                    PIN_LCD_MOSI, PIN_LCD_MISO, HSPI, true);
    s_gfx = new Arduino_GC9309(s_bus, PIN_LCD_RST, 0, false,
                               SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0, 0);

    if (!s_gfx->begin(SPI_FREQUENCY)) {
        Serial.println("ERROR: GC9309 initialization failed");
        while (true) delay(1000);
    }
    s_gfx->setRotation(0);
    s_gfx->fillScreen(RGB565_BLACK);
}

// Create the LVGL display with a small partial-render buffer
void CrowPanelApplication::initLvgl() {
    lv_init();
    lv_tick_set_cb(millis);

    const size_t buf_bytes = (size_t)SCREEN_WIDTH * DRAW_BUFFER_LINES * sizeof(uint16_t);
    _buf1 = (uint16_t*)heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL);
    if (_buf1 == nullptr) {
        Serial.println("ERROR: LVGL draw buffer allocation failed");
        while (true) delay(1000);
    }

    _lvgl_disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_color_format(_lvgl_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(_lvgl_disp, lvglFlushCb);
    lv_display_set_buffers(_lvgl_disp, _buf1, nullptr, buf_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
}

// Adaptive LVGL tick: poll between LVGL_TICK_MIN/MAX ms, driven by lv_timer_handler()
void CrowPanelApplication::handleLvglTick(const uint32_t now) {
    if (now - _last_lvgl_tick < _next_lvgl_interval_ms) return;
    _last_lvgl_tick = now;

    uint32_t next_ms = lv_timer_handler();
    if (next_ms < LVGL_TICK_MIN_MS)      _next_lvgl_interval_ms = LVGL_TICK_MIN_MS;
    else if (next_ms > LVGL_TICK_MAX_MS) _next_lvgl_interval_ms = LVGL_TICK_MAX_MS;
    else                                 _next_lvgl_interval_ms = next_ms;
}

// Knob rotation: adjust brightness if the active page intercepts, else switch pages
void CrowPanelApplication::handleKnobRotation() {
    int8_t dir = _encoder.getDirection();
    if (dir == 0) return;

    _power.notifyActivity();   // any knob motion keeps the display awake

    IScreenUI* screen = _screenMgr.getCurrentScreen();
    if (screen && screen->interceptsRotation()) {
        screen->onRotation(dir);
    } else {
        if (dir > 0) {
            _screenMgr.switchNext();
        } else {
            _screenMgr.switchPrevious();
        }
        // Tier 1 only — free. Flash (tier 2) is written once on the way into sleep.
        s_last_page_index = _screenMgr.currentIndex();
    }
}

// Knob button: forward to the active page
void CrowPanelApplication::handleKnobButtonPress() {
    if (!_encoder.getButtonPressed()) return;
    _power.notifyActivity();   // button press keeps the display awake
    IScreenUI* screen = _screenMgr.getCurrentScreen();
    if (screen) screen->onButtonPress();
}

// Touch: a second input device feeding the SAME IScreenUI seam as the knob, plus an
// absolute-position drag the knob can't do (the Setup brightness slider). On touch-
// down the active page may CLAIM the touch as a drag (interceptsTouchAt); if so we
// stream onTouchDrag/onTouchEnd to it. Otherwise the release is classified into a
// discrete gesture: TAP → onButtonPress (tap == button press), vertical swipe →
// page switch (up = next, mirroring knob CW), persisted like the knob path.
void CrowPanelApplication::handleTouch(const uint32_t now) {
    _touch.poll(now);

    // Touch-down: decide whether the active page wants this touch as a drag.
    if (_touch.justPressed()) {
        _power.notifyActivity();
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
        IScreenUI* screen = _screenMgr.getCurrentScreen();
        if (_touch_dragging) {
            if (screen) screen->onTouchEnd();
            _touch_dragging = false;
            _touch.getGesture();   // discard any classification for a claimed drag
            return;
        }
        switch (_touch.getGesture()) {
            case TouchGesture::TAP:
                if (screen) screen->onButtonPress();
                break;
            case TouchGesture::SWIPE_UP:
                _screenMgr.switchNext();
                s_last_page_index = _screenMgr.currentIndex();
                break;
            case TouchGesture::SWIPE_DOWN:
                _screenMgr.switchPrevious();
                s_last_page_index = _screenMgr.currentIndex();
                break;
            default:
                break;   // LONG_PRESS reserved; NONE ignored
        }
    }
}

// Discipline the system clock from GNSS: apply a freshly received UTC datetime.
// Runs regardless of the active page so the Clock is correct whenever it is shown.
// Cheap when idle (one spinlock-guarded flag read); settimeofday only on a new packet.
void CrowPanelApplication::handleTimeSync() {
    if (!_receiver.hasNewDatetimeData()) return;
    DateTimeDelta dt = _receiver.getDatetimeData();
    if (!dt.time_valid) return;
    struct timeval tv = { (time_t)dt.unix_utc, 0 };
    settimeofday(&tv, nullptr);
}

// Periodic update of the active page only (~17 Hz)
void CrowPanelApplication::handleUIUpdate(const uint32_t now) {
    if (now - _last_ui_update < UI_UPDATE_INTERVAL_MS) return;
    _last_ui_update = now;

    IScreenUI* screen = _screenMgr.getCurrentScreen();
    if (screen) screen->update();
}

// Near-empty cell: show an unmistakable prompt, then power down before the cell's own
// protection circuit does it for us. The button wake stays armed, so a press just
// repeats this short cycle (~0.1 mAh) and plugging in USB restores a normal boot.
void CrowPanelApplication::checkBatteryCutoff() {
    if (_battery.charging()) return;
    if (_battery.level() != BatteryMonitor::Level::CUTOFF) return;

    this->showBatteryCutoffScreen();
    _power.enterProtectiveSleep();   // does not return
}

// Code-built overlay (no SquareLine export involved): an opaque full-screen panel on top
// of whatever page is loaded, so it works during the wake sequence too — before the page
// adapters have been revealed. Lit at the CUTOFF backlight ceiling only.
void CrowPanelApplication::showBatteryCutoffScreen() {
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
    if (ledcRead(PIN_BACKLIGHT) == 0) _setupUI.rampBacklightUp();
    delay(CUTOFF_MESSAGE_MS);
}
