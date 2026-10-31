#pragma once

#include <Arduino.h>
#include <Wire.h>

class XPowersAXP2101;   // XPowersLib, included only in the .cpp files

// === C L A S S  P M U R A I L S ===
//
// - Board power MECHANISM for the Waveshare ESP32-S3-Touch-AMOLED-2.06: which AXP2101
//   rails are on while awake and in deep sleep, plus the audio codecs' power-down. The
//   policy (when to sleep) stays in PowerManager; the driver object stays owned by
//   BatteryMonitor, which hands it over with pmu().
// - Rail-to-load map, read from the schematic (Schematic V1.0, 2026-10-31):
//     DCDC1 = VCC3V3  ESP32-S3, panel VCI/VDDIO, touch, IMU, codecs' digital, amp, SD,
//                     I2C pull-ups. NEVER touched.
//     ALDO1 = A3V3    ES7210 (all supplies) + ES8311 AVDD. Left ON: cutting it while the
//                     ES7210's I2C pins sit on 3.3 V pull-ups would back-power it through
//                     its ESD diodes. The codecs are powered down by register instead.
//     ALDO2           only DSI_PWR_EN (10k pull-up): the AMOLED module's own supply
//                     enable. Cut in deep sleep, restored before the panel init.
//     ALDO3           vibration-motor pad (0R); no motor on this board.
//     ALDO4, BLDO1/2, DLDO1/2, CPUSLDO   no load. Off.
//     DCDC2..5        not touched (pins unconnected; DCDC5 shares the VSYS node).
// - The rail state survives ESP32 deep sleep (the PMIC stays powered), so beginAwake()
//   must undo what enterSleep() did on every wake. A PMIC power cycle (PWR held ~6 s)
//   reloads its own defaults; beginAwake() sets every state explicitly either way.
// - Owned by: WatchApplication.

class PmuRails {

public:

    // Hand over the PMIC (null = no PMIC, every call becomes a no-op) and the shared bus
    void attach(XPowersAXP2101* pmu, TwoWire* wire) { _pmu = pmu; _wire = wire; }

    // Awake rail state. Call right after BatteryMonitor::beginPmu() and BEFORE the panel
    // init: the AMOLED module needs DSI_PWR_EN high before its reset sequence.
    void beginAwake();

    // Sleep rail state. Call after the panel is in sleep-in, as the last bus traffic
    // before the wake source is armed.
    void enterSleep();

    // Compact rail state for the Status screen: "DC 01 LDO 03 00" = AXP2101 registers
    // 0x80 (DCDC on/off), 0x90 and 0x91 (LDO on/off), raw hex. "--" without a PMIC.
    void summary(char* out, size_t len) const;

    // Full decoded rail and ADC dump to Serial (only seen on USB)
    void logState() const;

private:

    XPowersAXP2101* _pmu = nullptr;
    TwoWire* _wire = nullptr;

    // Result of this boot's codec power-down, for logState(): the boot-time log lines are
    // lost before the native USB port enumerates. -1 = not found / not run, else the
    // number of failed register writes.
    int8_t _es7210_fails = -1;
    int8_t _es8311_fails = -1;

    // DSI_PWR_EN settle before the panel reset pulse when ALDO2 was off [M]
    static constexpr uint32_t PANEL_POWER_SETTLE_MS = 10;

    // Codec I2C addresses: ES7210 with AD0 = AD1 = GND (schematic), ES8311 with CE low
    static constexpr uint8_t ES7210_ADDR = 0x40;
    static constexpr uint8_t ES8311_ADDR = 0x18;

    void unusedRailsOff();
    void codecsPowerDown();
    bool probe(uint8_t addr) const;
    uint8_t writeSequence(uint8_t addr, const uint8_t (*seq)[2], size_t count) const;

};
