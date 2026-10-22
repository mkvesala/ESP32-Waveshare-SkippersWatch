#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include "ui.h"
#include "ESPNowReceiver.h"
#include "NavigationUI.h"
#include "BatteryUI.h"
#include "WeatherUI.h"
#include "EngineUI.h"
#include "AttitudeUI.h"
#include "ClockUI.h"
#include "SetupUI.h"
#include "MenuUI.h"
#include "TouchInput.h"
#include "ScreenManager.h"
#include "PowerManager.h"
#include "BatteryMonitor.h"
#include "PanelBrightness.h"
#include "DemoSource.h"
#include "ThemeController.h"
#include "version.h"

// === C L A S S  W A T C H A P P L I C A T I O N ===
//
// - "The app": orchestrates the Skipper's Watch on the Waveshare ESP32-S3-Touch-AMOLED-2.06
//   (CO5300 QSPI AMOLED 410×502, FT3168 touch, AXP2101 PMIC)
// - Init: app.begin();  Loop: app.loop()
// - Owns: Arduino_GFX QSPI display stack, ESPNowReceiver, the seven page adapters, the page
//   menu, TouchInput, ScreenManager, PowerManager, BatteryMonitor (+ the AXP2101)
// - Input: touch only (no knob on this board). Swipe left/right steps the carousel, swipe
//   down opens the page menu, swipe up closes it, tap goes to the page or a menu button.
// - Page carousel: NAVIGATION(0) → BATTERIES(1) → WEATHER(2) → ENGINE(3) → ATTITUDE(4) → CLOCK(5) → SETUP(6)
// - Pins/parts from Waveshare's examples — see hardware_notes_waveshare.md
// - Model B power: deep sleep at rest, the PWR button wakes (see PowerManager).
// - A true power-on (after the AXP2101 cut power) shows the startup screen for
//   STARTUP_SCREEN_MS first; a deep-sleep wake opens the last page directly, so the two
//   are told apart at a glance.
// - Setup's demo and night switches are applied here (applyModes): demo swaps the data
//   source behind the receiver, night swaps the palette.

class WatchApplication {

public:

    explicit WatchApplication();

    void begin();
    void loop();

private:

    // ESP-NOW channel — must match the fleet AP channel (verrokki = 6)
    static constexpr uint8_t ESP_NOW_CHANNEL = 6;

    // Dark listen window on wake: hold the panel black while the radio (just
    // brought up) catches a fresh broadcast, so the first LIT frame shows real
    // values instead of a "---" placeholder flash. Senders broadcast faster
    // than this, so a fresh packet typically lands within it.
    static constexpr uint32_t ESPNOW_WAKE_LISTEN_MS = 200;

    // How long the "BATTERY EMPTY / CHARGE NOW" prompt is held before the protective
    // sleep. Long enough to read, short enough that a repeated press of the wake button
    // on a dead cell costs little.
    static constexpr uint32_t CUTOFF_MESSAGE_MS = 2000;

    // Startup screen on a true power-on. Not taken from the wake time: the display-on
    // window starts only after it (PowerManager::begin() runs afterwards).
    static constexpr uint32_t STARTUP_SCREEN_MS = 3000;

    // --- Pin map (see hardware_notes_waveshare.md) ---
    static constexpr uint8_t PIN_LCD_CS    = 12;
    static constexpr uint8_t PIN_LCD_SCK   = 11;
    static constexpr uint8_t PIN_LCD_SIO0  = 4;
    static constexpr uint8_t PIN_LCD_SIO1  = 5;
    static constexpr uint8_t PIN_LCD_SIO2  = 6;
    static constexpr uint8_t PIN_LCD_SIO3  = 7;
    static constexpr uint8_t PIN_LCD_RST   = 8;
    static constexpr uint8_t PIN_I2C_SDA   = 15;   // shared: FT3168, AXP2101 (+ unused IMU/RTC/codecs)
    static constexpr uint8_t PIN_I2C_SCL   = 14;
    static constexpr uint8_t PIN_PA_CTRL   = 46;   // speaker amp enable — kept LOW (off)

    // Display. Column offset 22 is part of Waveshare's CO5300 init; without it the image
    // is shifted.
    static constexpr uint16_t SCREEN_WIDTH  = 410;
    static constexpr uint16_t SCREEN_HEIGHT = 502;
    static constexpr uint8_t  LCD_COL_OFFSET = 22;

    // LVGL partial draw buffer: 40 lines × 410 px × 2 bytes = 32 800 B internal SRAM
    // (keeps PSRAM out of the wake path)
    static constexpr uint16_t DRAW_BUFFER_LINES = 40;
    lv_display_t* _lvgl_disp = nullptr;
    uint16_t* _buf1 = nullptr;

    // LVGL tick (adaptive, driven by lv_timer_handler() return value)
    uint32_t _last_lvgl_tick = 0;
    uint32_t _next_lvgl_interval_ms = 5;
    static constexpr uint32_t LVGL_TICK_MIN_MS = 1;
    static constexpr uint32_t LVGL_TICK_MAX_MS = 20;

    // UI update cadence ~17 Hz
    static constexpr uint32_t UI_UPDATE_INTERVAL_MS = 59;
    uint32_t _last_ui_update = 0;

    // Core instances
    PanelBrightness _brightness;
    ESPNowReceiver _receiver;
    DemoSource     _demo;
    NavigationUI   _navUI;
    BatteryUI      _batteryUI;
    WeatherUI      _weatherUI;
    EngineUI       _engineUI;
    AttitudeUI     _attitudeUI;
    ClockUI        _clockUI;
    SetupUI        _setupUI;
    MenuUI         _menu;
    TouchInput     _touch;
    ScreenManager  _screenMgr;
    PowerManager   _power;
    BatteryMonitor _battery;   // drives the top bar from the watch's own LiPo (AXP2101)
    ThemeController _theme;

    // Demo mode as currently applied to the receiver (Setup holds the user's choice)
    bool _demo_active = false;

    // True while a touch claimed at touch-down is being handled as a page drag
    // (e.g. the Setup brightness slider) rather than a swipe/tap.
    bool _touch_dragging = false;

    // --- Last-active page persistence (two tiers, see resolveStartPage) ---
    // Own namespace rather than SetupUI's "display": that one is its private display-
    // settings store, and a page index is not a display setting.
    static constexpr const char* NVS_NAMESPACE_UI = "watch";
    static constexpr const char* NVS_KEY_LASTPAGE = "lastpage";

    // Last value actually committed to NVS this session, so the pre-sleep flush writes
    // only when the page really changed. 0xFF = "nothing written yet / unknown".
    uint8_t _nvs_page_written = 0xFF;

    // Decide the page to open on. Tier 1: RTC .rtc.noinit (free, survives deep sleep,
    // software reset, watchdog and panic). Tier 2: NVS (survives a true power loss, e.g.
    // the AXP2101 long-press power-off). Falls back to Navigation. Also logs the reset
    // reason and which tier supplied the value.
    uint8_t resolveStartPage();
    uint8_t loadPageFromNvs();
    void    savePageToNvs();

    // PowerManager pre-sleep hook trampoline (plain function pointer + ctx).
    static void preSleepFlush(void* ctx);

    void initDisplay();
    void initLvgl();
    void handleLvglTick(const uint32_t now);
    void handleTouch(const uint32_t now);
    void handleGesture(TouchGesture gesture);
    void handleUIUpdate(const uint32_t now);
    void handleTimeSync();

    // Bring the receiver, top bar, Clock and palette in line with Setup's demo and night
    // switches. Cheap when nothing changed — called on every wake and every loop.
    void applyModes();

    // Startup screen (name + version) lit for STARTUP_SCREEN_MS, LVGL kept running.
    // The radio is already up, so this doubles as the wake listen window.
    void showStartupScreen();

    // Page menu (ui_ContainerMenu): open over the current page / close back to it / jump.
    void openMenu();
    void closeMenu();
    void jumpToPage(uint8_t index);
    void rememberPage();

    // Full-screen "charge now" overlay shown just before a protective sleep on a
    // near-empty cell. Built in code (no SquareLine round-trip) — it must be available
    // on the wake path before the page adapters are even revealed.
    void showBatteryCutoffScreen();

    // Enter the protective-sleep path if the cell is at CUTOFF and not on USB. Called
    // both during the wake sequence (before the radio is brought up on a dying cell)
    // and every loop. Returns only when there is nothing to do — otherwise it sleeps.
    void checkBatteryCutoff();

};
