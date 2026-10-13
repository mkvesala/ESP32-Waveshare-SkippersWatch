#include "ft3168_touch.h"

// === P U B L I C ===

bool Ft3168Touch::begin(TwoWire& wire, int8_t rst_pin) {
    _wire = &wire;

    // Hardware reset, timing as in the vendor driver (also brings it out of hibernate)
    if (rst_pin >= 0) {
        pinMode(rst_pin, OUTPUT);
        digitalWrite(rst_pin, HIGH);
        delay(1);
        digitalWrite(rst_pin, LOW);
        delay(20);
        digitalWrite(rst_pin, HIGH);
        delay(50);
    }

    uint8_t id = 0;
    if (!this->readRegs(REG_CHIP_ID, &id, 1)) return false;
    Serial.printf("[touch] FT3x68 chip id=0x%02X\n", id);

    // Vendor init: monitor mode (reports a touch, drops to low-rate scanning when idle)
    this->writeReg(REG_POWER_MODE, POWER_MONITOR);
    delay(20);
    return true;
}

bool Ft3168Touch::readPoint(uint16_t& x, uint16_t& y) {
    uint8_t buf[5];
    // 0x02 finger count, 0x03 X1H, 0x04 X1L, 0x05 Y1H, 0x06 Y1L in one burst
    if (!this->readRegs(REG_FINGER_NUM, buf, sizeof(buf))) return false;
    const uint8_t fingers = buf[0] & 0x0F;
    if (fingers == 0 || fingers > 2) return false;
    x = (uint16_t)(((buf[1] & 0x0F) << 8) | buf[2]);
    y = (uint16_t)(((buf[3] & 0x0F) << 8) | buf[4]);
    return true;
}

void Ft3168Touch::hibernate() {
    if (_wire == nullptr) return;
    this->writeReg(REG_POWER_MODE, POWER_HIBERNATE);
}

// === P R I V A T E ===

bool Ft3168Touch::writeReg(uint8_t reg, uint8_t value) {
    _wire->beginTransmission(I2C_ADDRESS);
    _wire->write(reg);
    _wire->write(value);
    return _wire->endTransmission() == 0;
}

bool Ft3168Touch::readRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
    _wire->beginTransmission(I2C_ADDRESS);
    _wire->write(reg);
    if (_wire->endTransmission(false) != 0) return false;
    if (_wire->requestFrom(I2C_ADDRESS, len) != len) return false;
    for (uint8_t i = 0; i < len; ++i) buf[i] = _wire->read();
    return true;
}
