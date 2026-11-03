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
//                     enable. Left ON: cutting it in sleep measured +30-40 mA
//                     (SLEEP_CUT_PANEL_POWER, off). Always restored before the panel init.
//     ALDO3           vibration-motor pad (0R); no motor on this board.
//     ALDO4, BLDO1/2, DLDO1/2, CPUSLDO   no load. Off.
//     DCDC2..4        pins unconnected, on after reset. Off (UNUSED_DCDC_OFF).
//     DCDC5           not touched (shares the VSYS node).
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

    // 7-bit addresses that answered this boot's bus scan, "18* 34 40* ..." ("--" before
    // it). A codec or the IMU marked '*' took its whole power-down, '!' had failed writes.
    const char* busDevices() const { return _bus_devices[0] ? _bus_devices : "--"; }

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
    int8_t _imu_fails = -1;

    char _bus_devices[48] = {};

    // DSI_PWR_EN settle before the panel reset pulse when ALDO2 was off [M]
    static constexpr uint32_t PANEL_POWER_SETTLE_MS = 10;

    // Codec I2C addresses: ES7210 with AD0 = AD1 = GND (schematic), ES8311 with CE low
    static constexpr uint8_t ES7210_ADDR = 0x40;
    static constexpr uint8_t ES8311_ADDR = 0x18;

    // QMI8658 (RIOT qmi8658_constants.h, read 2026-11-03): WHO_AM_I 0x00 = 0x05; CTRL1
    // 0x02 bit0 = SensorDisable (internal oscillator off); CTRL7 0x08 = sensor enables
    static constexpr uint8_t QMI8658_ADDR = 0x6B;
    static constexpr uint8_t QMI_REG_WHO_AM_I = 0x00;
    static constexpr uint8_t QMI_WHO_AM_I = 0x05;
    static constexpr uint8_t QMI_REG_CTRL1 = 0x02;
    static constexpr uint8_t QMI_CTRL1_SENSOR_DISABLE = 0x01;
    static constexpr uint8_t QMI_REG_CTRL7 = 0x08;

    void scanBus();
    void unusedRailsOff();
    void codecsPowerDown();
    void imuPowerDown();
    bool readReg(uint8_t addr, uint8_t reg, uint8_t& value) const;
    bool writeReg(uint8_t addr, uint8_t reg, uint8_t value) const;
    bool probe(uint8_t addr) const;
    uint8_t writeSequence(uint8_t addr, const uint8_t (*seq)[2], size_t count) const;

};
