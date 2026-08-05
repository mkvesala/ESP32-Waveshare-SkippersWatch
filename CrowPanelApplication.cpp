#include "CrowPanelApplication.h"
#include <esp_heap_caps.h>

// File-scope display state — shared with the static LVGL flush callback and the
// tearing-effect ISR (both run outside the class instance). Mirrors the Elecrow
// example's global display objects, kept local to this translation unit.
static Arduino_DataBus*   s_bus = nullptr;
static Arduino_GFX*       s_gfx = nullptr;
static SemaphoreHandle_t  s_te_sem = nullptr;

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
    // FIRST: release any GPIO holds from the previous deep sleep, so the rail/backlight
    // writes below actually take effect (otherwise the panel wakes to a black screen).
    _power.releaseHolds();

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
    _setupUI.begin();

    // Register the carousel of container-backed pages, in order
    _screenMgr.addScreen(&_navUI);
    _screenMgr.addScreen(&_batteryUI);
    _screenMgr.addScreen(&_weatherUI);
    _screenMgr.addScreen(&_engineUI);
    _screenMgr.addScreen(&_attitudeUI);
    _screenMgr.addScreen(&_setupUI);
    _screenMgr.begin();

    // --- Staggered wake sequence (runs on every wake, cold boot included) ---
    // Panel is initialised DARK above; render the first frame (all sources "---"),
    // THEN ramp the backlight up as its own load step, THEN bring the radio up last
    // so no single instant sees panel + backlight + radio inrush together.
    lv_refr_now(_lvgl_disp);          // render the first frame into the dark panel
    _setupUI.rampBacklightUp();       // backlight PWM soft-start (own step)

    _encoder.begin();
    _receiver.begin(ESP_NOW_CHANNEL); // radio LAST

    // Model B power owner — records wake cause, starts the display-on timer
    _power.begin(esp_sleep_get_wakeup_cause());
}

void CrowPanelApplication::loop() {
    const uint32_t now = millis();
    this->handleLvglTick(now);
    this->handleKnobRotation();
    this->handleKnobButtonPress();
    this->handleUIUpdate(now);
    _power.loop(now);   // may enter deep sleep after the display-on timeout (no return)
}

// === P R I V A T E ===

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
    } else if (dir > 0) {
        _screenMgr.switchNext();
    } else {
        _screenMgr.switchPrevious();
    }
}

// Knob button: forward to the active page
void CrowPanelApplication::handleKnobButtonPress() {
    if (!_encoder.getButtonPressed()) return;
    _power.notifyActivity();   // button press keeps the display awake
    IScreenUI* screen = _screenMgr.getCurrentScreen();
    if (screen) screen->onButtonPress();
}

// Periodic update of the active page only (~17 Hz)
void CrowPanelApplication::handleUIUpdate(const uint32_t now) {
    if (now - _last_ui_update < UI_UPDATE_INTERVAL_MS) return;
    _last_ui_update = now;

    IScreenUI* screen = _screenMgr.getCurrentScreen();
    if (screen) screen->update();
}
