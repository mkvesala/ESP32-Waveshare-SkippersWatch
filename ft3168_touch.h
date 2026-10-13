#pragma once

#include <Arduino.h>
#include <Wire.h>

// === C L A S S  F T 3 1 6 8 T O U C H ===
//
// - Minimal FT3168 capacitive-touch driver for the Waveshare AMOLED 2.06 (I2C 0x38,
//   RST=GPIO9, INT=GPIO38). Replaces the CrowPanel's AXS5106L driver.
// - Registers taken from Waveshare's Arduino_DriveBus Arduino_FT3x68 (vendor repo,
//   examples/arduino/libraries): 0x02 finger count, 0x03..0x06 X1/Y1 (12-bit, high
//   nibble in 0x03/0x05), 0xA5 power mode (0 active, 1 monitor, 2 standby, 3 hibernate),
//   0xA0 chip ID (0x03 = FT3168).
// - POLLED, not interrupt-driven: readPoint() asks the chip for the finger count on
//   every call, so it reports a LEVEL (finger down / up). The INT line is not used —
//   GPIO38 is not RTC-capable, so it could not wake the SoC from deep sleep anyway.
// - The bus (SDA=GPIO15, SCL=GPIO14) is shared with the AXP2101; Wire must already be
//   begun by the app. All access stays on the main loop.

class Ft3168Touch {

public:

    static constexpr uint8_t I2C_ADDRESS = 0x38;

    // Reset pulse + ID probe + monitor power mode (vendor default). False if no ACK.
    bool begin(TwoWire& wire, int8_t rst_pin);

    // True + point while a finger is on the panel, false otherwise.
    bool readPoint(uint16_t& x, uint16_t& y);

    // Deepest power mode before deep sleep. Only a reset pulse brings it back, which
    // begin() does on every wake.
    void hibernate();

private:

    static constexpr uint8_t REG_FINGER_NUM = 0x02;
    static constexpr uint8_t REG_X1_H       = 0x03;
    static constexpr uint8_t REG_POWER_MODE = 0xA5;
    static constexpr uint8_t REG_CHIP_ID    = 0xA0;

    static constexpr uint8_t POWER_MONITOR   = 0x01;
    static constexpr uint8_t POWER_HIBERNATE = 0x03;

    bool writeReg(uint8_t reg, uint8_t value);
    bool readRegs(uint8_t reg, uint8_t* buf, uint8_t len);

    TwoWire* _wire = nullptr;

};
