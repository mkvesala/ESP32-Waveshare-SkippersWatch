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
#include "RotaryEncoder.h"
#include "TouchInput.h"
#include "ScreenManager.h"
#include "PowerManager.h"
#include "BatteryMonitor.h"

// === C L A S S  C R O W P A N E L A P P L I C A T I O N ===
//
// - "The app": orchestrates the CrowPanel 2.01" Watch (GC9309 SPI, 240x296)
// - Init: app.begin();  Loop: app.loop()
// - Owns: Arduino_GFX SPI display stack, ESPNowReceiver, the seven page adapters,
//         RotaryEncoder, TouchInput, ScreenManager
// - Input: knob AND touch feed the same IScreenUI seam (handleKnobRotation/
//   handleKnobButtonPress/handleTouch). Touch is additive; the encoder can later be
//   deleted (its rotation is fully covered by swipe/tap/drag) leaving only its
//   push-button as the deep-sleep wake source — see PowerManager WAKE_BUTTON_PIN.
// - Page carousel: NAVIGATION(0) → BATTERIES(1) → WEATHER(2) → ENGINE(3) → ATTITUDE(4) → CLOCK(5) → SETUP(6)
// - Pins/parts confirmed from Elecrow example — see hardware_notes.md
// - M0 baseline: runs flat-out (no sleep). PowerManager (Model B) is layered on in M1.

class CrowPanelApplication {

public:

    explicit CrowPanelApplication();

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

    // --- Pin map (see hardware_notes.md) ---
    static constexpr uint8_t PIN_LCD_CS        = 9;
    static constexpr uint8_t PIN_LCD_DC        = 10;
    static constexpr uint8_t PIN_LCD_SCK       = 7;
    static constexpr uint8_t PIN_LCD_MOSI      = 8;
    static constexpr int8_t  PIN_LCD_MISO      = -1;
    static constexpr uint8_t PIN_LCD_RST       = 6;
    static constexpr uint8_t PIN_LCD_TE        = 21;   // tearing-effect sync
    static constexpr uint8_t PIN_LCD_POWER     = 40;   // ACTIVE-LOW: LOW = rail on
    static constexpr uint8_t PIN_PERIPH_POWER  = 47;   // shared peripheral rail: HIGH = on
    static constexpr uint8_t PIN_BACKLIGHT     = 13;   // LEDC PWM

    // Backlight PWM
    static constexpr uint32_t PWM_FREQ = 5000;
    static constexpr uint8_t  PWM_RESOLUTION = 8;

    // Display
    static constexpr uint16_t SCREEN_WIDTH  = 240;
    static constexpr uint16_t SCREEN_HEIGHT = 296;
    static constexpr uint32_t SPI_FREQUENCY = 80UL * 1000UL * 1000UL;

    // LVGL partial draw buffer: 40 lines × 240 px × 2 bytes = 19 200 B internal SRAM
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
    ESPNowReceiver _receiver;
    NavigationUI   _navUI;
    BatteryUI      _batteryUI;
    WeatherUI      _weatherUI;
    EngineUI       _engineUI;
    AttitudeUI     _attitudeUI;
    ClockUI        _clockUI;
    SetupUI        _setupUI;
    RotaryEncoder  _encoder;
    TouchInput     _touch;
    ScreenManager  _screenMgr;
    PowerManager   _power;
    BatteryMonitor _battery;   // drives the top bar from the watch's own LiPo (GPIO1 ADC)

    // True while a touch claimed at touch-down is being handled as a page drag
    // (e.g. the Setup brightness slider) rather than a swipe/tap.
    bool _touch_dragging = false;

    void initPowerRails();
    void initBacklight();
    void initDisplay();
    void initLvgl();
    void handleLvglTick(const uint32_t now);
    void handleKnobRotation();
    void handleKnobButtonPress();
    void handleTouch(const uint32_t now);
    void handleUIUpdate(const uint32_t now);
    void handleTimeSync();

};
