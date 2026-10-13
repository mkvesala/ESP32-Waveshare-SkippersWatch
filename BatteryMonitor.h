#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include <Wire.h>
#include "ui.h"   // ui_BarBatteryIndicator (top status-bar gauge on ui_ScreenMain)
#include "BatteryLevel.h"

class XPowersAXP2101;   // XPowersLib, included only in BatteryMonitor.cpp

// === C L A S S  B A T T E R Y M O N I T O R ===
//
// - Drives the always-visible top battery bar (ui_BarBatteryIndicator) from the
//   watch's OWN LiPo — distinct from BatteryUI, which shows the boat's remote banks.
// - Reads the cell from the AXP2101 PMIC (XPowersLib) over the shared I2C bus:
//   battery voltage (smoothed, EMA), the PMIC's fuel-gauge percentage (falls back to a
//   LiPo resting curve if the gauge reports nothing), and VBUS presence. Fills the bar
//   (0..100) and recolors it green/amber/red by charge level.
// - Owns the PMIC driver object. beginPmu() once, early (Wire already begun); begin()
//   once after ui_init(); update(now) each loop (self time-gates to SAMPLE_INTERVAL_MS).
// - Owned by: WatchApplication.
// - Also classifies the charge state into a coarse Level (OK/WARN/CRITICAL/CUTOFF).
//   That is DATA, not policy: what to do about each level (shorter display timeout,
//   backlight ceiling, protective sleep) lives in PowerManager.

class BatteryMonitor {

public:

    // Coarse charge state driving PowerManager's low-charge ladder. Deliberately
    // aligned with the color buckets below, so the red bar IS the WARN indicator.
    using Level = BatteryLevel;

    // Bring up the AXP2101 on the shared bus and enable its ADC channels. Touches no
    // power rails. False if the PMIC does not ACK (the bar then shows charging/100).
    bool beginPmu(TwoWire& wire, int sda, int scl);

    void begin();
    void update(uint32_t now);

    // Last estimated State-of-Charge, 0..100
    uint8_t soc() const { return _soc; }

    // Smoothed cell voltage in millivolts (the cutoff wants a real voltage floor,
    // not only a curve extrapolation at the steep bottom end)
    uint16_t cellMillivolts() const { return (uint16_t)(_ema_mv + 0.5f); }

    // True while the watch is on external USB power (AXP2101 VBUS present)
    bool charging() const { return _charging; }

    // Current charge level. Escalates only within one awake session (see _level) and
    // is forced back to OK whenever the watch is on USB power.
    Level level() const { return _level; }

private:

    XPowersAXP2101* _pmu = nullptr;

    // --- Sampling ---
    static constexpr uint32_t SAMPLE_INTERVAL_MS = 3000;  // SoC drifts slowly
    static constexpr float    EMA_ALPHA          = 0.2f;  // smoothing weight of a new sample

    // --- Indicator colors (code-owned; override the SLS white). Amber matches
    //     SetupUI::ADJUST_ACCENT so the two accents stay identical. ---
    static constexpr uint32_t COLOR_GREEN = 0x4CAF50;  // SoC > 40 %
    static constexpr uint32_t COLOR_AMBER = 0xFF9800;  // SoC > 20 %
    static constexpr uint32_t COLOR_RED   = 0xF44336;  // SoC <= 20 %
    static constexpr uint32_t COLOR_BLUE  = 0x2196F3;  // on USB power (charging)

    // --- Color-bucket thresholds (percent) ---
    static constexpr uint8_t GREEN_ABOVE = 40;
    static constexpr uint8_t AMBER_ABOVE = 20;

    // Low-charge ladder thresholds live in BatteryLevel.h (BatteryLadder).

    float   _ema_mv      = 0.0f;   // smoothed cell millivolts
    bool    _seeded      = false;  // false until the first sample seeds the EMA
    bool    _charging    = false;  // true while on USB power (bar forced blue/100)
    bool    _charge_logged = false; // USB state already logged (log on change, not every 3 s)
    uint32_t _last_sample_ms = 0;
    uint8_t _soc         = 0;      // last SoC 0..100
    int8_t  _last_bucket = -1;     // last applied color bucket (-1 = none yet)

    // Ladder level. Only ever ESCALATES while awake, so EMA jitter under the active load cannot make the ladder flap across a threshold. A deep-sleep wake is
    // a full reboot, so every wake re-evaluates from scratch.
    Level _level = Level::OK;

    bool     usbPresent() const;             // AXP2101 VBUS in (true if no PMIC: fail safe)
    uint16_t sampleCellMillivolts() const;   // AXP2101 battery voltage, mV
    uint8_t  estimateSoc(uint16_t cell_mv) const;  // gauge %, else LiPo resting curve
    uint8_t  curveSoc(uint16_t cell_mv) const;     // LiPo resting-curve lookup
    Level    classify() const;                // current reading → Level (no hysteresis)
    void     escalateLevel();                 // apply classify() one-way (never de-escalates)
    void     render();                        // push state to the bar (value + color)

};
