#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "ui.h"   // ui_BarBatteryIndicator (top status-bar gauge on ui_ScreenMain)

// === C L A S S  B A T T E R Y M O N I T O R ===
//
// - Drives the always-visible top battery bar (ui_BarBatteryIndicator) from the
//   watch's OWN LiPo — distinct from BatteryUI, which shows the boat's remote banks.
// - Samples the GPIO1 battery ADC (100k/137k divider, powered off the GPIO47
//   peripheral rail that is HIGH whenever the watch is awake), smooths it (EMA),
//   estimates State-of-Charge from a LiPo resting curve, fills the bar (0..100),
//   and recolors it green/amber/red by charge level.
// - Owned by: CrowPanelApplication. begin() once after the rails + UI exist;
//   update(now) each loop (self time-gates to SAMPLE_INTERVAL_MS).
// - Display only: no low-charge protection ladder here (future PowerManager work;
//   it can query soc()).

class BatteryMonitor {

public:

    void begin();
    void update(uint32_t now);

    // Last estimated State-of-Charge, 0..100 (for a future low-charge ladder)
    uint8_t soc() const { return _soc; }

private:

    // --- Hardware (see hardware_notes.md) ---
    // GPIO1 = ADC1. Divider 100k upper / 137k lower → V_batt = V_adc * 237/137.
    static constexpr uint8_t  BATTERY_ADC_PIN = 1;
    static constexpr uint32_t DIVIDER_NUM     = 237;   // (100k + 137k)
    static constexpr uint32_t DIVIDER_DEN     = 137;   // 137k lower leg

    // --- Sampling ---
    static constexpr uint32_t SAMPLE_INTERVAL_MS = 3000;  // SoC drifts slowly
    static constexpr uint8_t  ADC_SAMPLES        = 16;    // averaged per reading (noise)
    static constexpr float    EMA_ALPHA          = 0.2f;  // smoothing weight of a new sample

    // --- Indicator colors (code-owned; override the SLS white). Amber matches
    //     SetupUI::ADJUST_ACCENT so the two accents stay identical. ---
    static constexpr uint32_t COLOR_GREEN = 0x4CAF50;  // SoC > 40 %
    static constexpr uint32_t COLOR_AMBER = 0xFF9800;  // SoC > 20 %
    static constexpr uint32_t COLOR_RED   = 0xF44336;  // SoC <= 20 %

    // --- Color-bucket thresholds (percent) ---
    static constexpr uint8_t GREEN_ABOVE = 40;
    static constexpr uint8_t AMBER_ABOVE = 20;

    float   _ema_mv      = 0.0f;   // smoothed cell millivolts
    bool    _seeded      = false;  // false until the first sample seeds the EMA
    uint32_t _last_sample_ms = 0;
    uint8_t _soc         = 0;      // last SoC 0..100
    int8_t  _last_bucket = -1;     // last applied color bucket (-1 = none yet)

    uint16_t sampleCellMillivolts() const;   // averaged, divider-corrected cell mV
    uint8_t  estimateSoc(uint16_t cell_mv) const;  // LiPo resting-curve lookup
    void     render();                        // push _soc to the bar + color

};
