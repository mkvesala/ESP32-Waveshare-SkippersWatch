#include "PmuRails.h"
#include "PowerManager.h"
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>

// Codec power-down sequences, copied in order from Espressif's esp_codec_dev
// (esp-adf master, components/esp_codec_dev/device/): es7210.c es7210_stop() and
// es8311.c es8311_suspend(). Read 2026-10-31. {register, value} pairs.
namespace {
    constexpr uint8_t kEs7210Stop[][2] = {
        { 0x47, 0xFF }, { 0x48, 0xFF }, { 0x49, 0xFF }, { 0x4A, 0xFF },   // MIC1..4 power
        { 0x4B, 0xFF }, { 0x4C, 0xFF },                                   // MIC12/34 power
        { 0x40, 0xC0 },                                                   // analog
        { 0x01, 0x7F },                                                   // all clocks off
        { 0x06, 0x07 },                                                   // power down
    };
    constexpr uint8_t kEs8311Suspend[][2] = {
        { 0x32, 0x00 }, { 0x17, 0x00 }, { 0x0E, 0xFF }, { 0x12, 0x02 },
        { 0x14, 0x00 }, { 0x0D, 0xFA }, { 0x15, 0x00 }, { 0x02, 0x10 },
        { 0x00, 0x00 }, { 0x00, 0x1F }, { 0x01, 0x30 }, { 0x01, 0x00 },
        { 0x45, 0x00 }, { 0x0D, 0xFC }, { 0x02, 0x00 },
    };

    // AXP2101 registers shown raw on the Status screen and in the Serial dump
    constexpr uint8_t REG_DC_ONOFF  = 0x80;   // bit0..4 = DCDC1..5
    constexpr uint8_t REG_DC_PWM    = 0x81;   // forced-PWM bits (PWM at light load costs mA)
    constexpr uint8_t REG_LDO_ONOFF0 = 0x90;  // bit0..7 = ALDO1..4, BLDO1..2, CPUSLDO, DLDO1
    constexpr uint8_t REG_LDO_ONOFF1 = 0x91;  // bit0 = DLDO2
    constexpr uint8_t REG_ADC_CTRL  = 0x30;   // ADC channel enables
}

// === P U B L I C ===

void PmuRails::beginAwake() {
    if (_pmu == nullptr) return;

    // AMOLED supply enable first, whatever the flags say: after a sleep that cut it, the
    // panel init below would otherwise talk to an unpowered module.
    if (!_pmu->isEnableALDO2()) {
        _pmu->enableALDO2();
        delay(PANEL_POWER_SETTLE_MS);
    }

    // Measurement channels the awake firmware reads (BatteryMonitor). Battery detection
    // and the battery voltage channel are never switched off — the gauge runs on them.
    _pmu->enableBattDetection();
    _pmu->enableBattVoltageMeasure();
    _pmu->enableVbusVoltageMeasure();
    _pmu->enableSystemVoltageMeasure();

    if (PowerManager::UNUSED_RAILS_OFF) this->unusedRailsOff();
    if (PowerManager::AUDIO_CODECS_OFF) this->codecsPowerDown();
}

void PmuRails::enterSleep() {
    if (_pmu == nullptr) return;

    // VBUS / system voltage are only read while awake; the gauge keeps its own channels
    _pmu->disableVbusVoltageMeasure();
    _pmu->disableSystemVoltageMeasure();

    // DSI_PWR_EN low: the AMOLED module's own converter off. The panel is already in
    // sleep-in (PowerManager::gatePeripherals()), and its reset line is held high.
    if (PowerManager::SLEEP_CUT_PANEL_POWER) _pmu->disableALDO2();
}

void PmuRails::summary(char* out, size_t len) const {
    if (_pmu == nullptr) {
        snprintf(out, len, "--");
        return;
    }
    snprintf(out, len, "DC %02X LDO %02X %02X",
             _pmu->readRegister(REG_DC_ONOFF) & 0x1F,
             _pmu->readRegister(REG_LDO_ONOFF0) & 0xFF,
             _pmu->readRegister(REG_LDO_ONOFF1) & 0x01);
}

void PmuRails::logState() const {
    if (_pmu == nullptr) {
        Serial.println("[rails] no PMIC");
        return;
    }
    const int dc  = _pmu->readRegister(REG_DC_ONOFF);
    const int pwm = _pmu->readRegister(REG_DC_PWM);
    const int ldo0 = _pmu->readRegister(REG_LDO_ONOFF0);
    const int ldo1 = _pmu->readRegister(REG_LDO_ONOFF1);
    const int adc = _pmu->readRegister(REG_ADC_CTRL);
    Serial.printf("[rails] 0x80 DC on=%02X  0x81 PWM=%02X  0x90 LDO=%02X  0x91=%02X  0x30 ADC=%02X\n",
                  dc, pwm, ldo0, ldo1, adc);
    Serial.printf("[rails] DC1..5 %d%d%d%d%d | ALDO1..4 %d%d%d%d | BLDO1..2 %d%d | CPUS %d | DLDO1..2 %d%d\n",
                  (dc >> 0) & 1, (dc >> 1) & 1, (dc >> 2) & 1, (dc >> 3) & 1, (dc >> 4) & 1,
                  (ldo0 >> 0) & 1, (ldo0 >> 1) & 1, (ldo0 >> 2) & 1, (ldo0 >> 3) & 1,
                  (ldo0 >> 4) & 1, (ldo0 >> 5) & 1, (ldo0 >> 6) & 1,
                  (ldo0 >> 7) & 1, ldo1 & 1);
    Serial.printf("[rails] batt %u mV, sys %u mV, vbus %u mV, gauge %d %%\n",
                  _pmu->getBattVoltage(), _pmu->getSystemVoltage(), _pmu->getVbusVoltage(),
                  _pmu->getBatteryPercent());
    Serial.printf("[rails] codec power-down this boot: ES7210 %s (%d failed), ES8311 %s (%d failed)\n",
                  _es7210_fails < 0 ? "not found" : "done", _es7210_fails < 0 ? 0 : _es7210_fails,
                  _es8311_fails < 0 ? "not found" : "done", _es8311_fails < 0 ? 0 : _es8311_fails);
}

// === P R I V A T E ===

// Rails with no load on this board (see the map in the header). Disabling one that is
// already off is a no-op, so this runs on every wake.
void PmuRails::unusedRailsOff() {
    _pmu->disableALDO3();
    _pmu->disableALDO4();
    _pmu->disableBLDO1();
    _pmu->disableBLDO2();
    _pmu->disableCPUSLDO();
    _pmu->disableDLDO1();
    _pmu->disableDLDO2();
}

// The ES7210 and ES8311 sit powered (ALDO1 + VCC3V3) but are never used. Their state
// after power-up is unknown, so put both into the drivers' own power-down state. The
// register state survives deep sleep (the chips stay powered), but this runs on every
// wake: a few ms of I2C, and it covers a PMIC power cycle without bookkeeping.
void PmuRails::codecsPowerDown() {
    if (_wire == nullptr) return;
    const size_t n7210 = sizeof(kEs7210Stop) / sizeof(kEs7210Stop[0]);
    const size_t n8311 = sizeof(kEs8311Suspend) / sizeof(kEs8311Suspend[0]);

    if (this->probe(ES7210_ADDR)) {
        const uint8_t fails = this->writeSequence(ES7210_ADDR, kEs7210Stop, n7210);
        _es7210_fails = (int8_t)fails;
        if (fails) Serial.printf("[rails] ES7210 power-down: %u writes failed\n", fails);
    } else {
        Serial.println("[rails] ES7210 not found at 0x40");
    }
    if (this->probe(ES8311_ADDR)) {
        const uint8_t fails = this->writeSequence(ES8311_ADDR, kEs8311Suspend, n8311);
        _es8311_fails = (int8_t)fails;
        if (fails) Serial.printf("[rails] ES8311 suspend: %u writes failed\n", fails);
    } else {
        Serial.println("[rails] ES8311 not found at 0x18");
    }
}

bool PmuRails::probe(uint8_t addr) const {
    _wire->beginTransmission(addr);
    return _wire->endTransmission() == 0;
}

uint8_t PmuRails::writeSequence(uint8_t addr, const uint8_t (*seq)[2], size_t count) const {
    uint8_t fails = 0;
    for (size_t i = 0; i < count; ++i) {
        _wire->beginTransmission(addr);
        _wire->write(seq[i][0]);
        _wire->write(seq[i][1]);
        if (_wire->endTransmission() != 0) fails++;
    }
    return fails;
}
