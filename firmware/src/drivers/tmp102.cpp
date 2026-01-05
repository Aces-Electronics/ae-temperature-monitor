#include "tmp102.h"

TMP102::TMP102(uint8_t addr) : _addr(addr), _wire(&Wire) {}

bool TMP102::begin(int sda, int scl) {
    return _wire->begin(sda, scl);
}

float TMP102::readTemperature() {
    _wire->beginTransmission(_addr);
    _wire->write(0x00); // Temperature register
    if (_wire->endTransmission() != 0) {
        return NAN;
    }

    if (_wire->requestFrom(_addr, (uint8_t)2) != 2) {
        return NAN;
    }

    uint8_t msb = _wire->read();
    uint8_t lsb = _wire->read();

    // 12-bit resolution
    int16_t val = (msb << 4) | (lsb >> 4);
    if (val > 0x7FF) {
        val |= 0xF000;
    }

    return val * 0.0625;
}

void TMP102::shutdown() {
    _wire->beginTransmission(_addr);
    _wire->write(0x01); // Config register
    _wire->write(0x01); // SD bit high (byte 1)
    _wire->write(0x00); // byte 2
    _wire->endTransmission();
}

void TMP102::wakeup() {
    _wire->beginTransmission(_addr);
    _wire->write(0x01); // Config register
    _wire->write(0x00); // SD bit low (byte 1)
    _wire->write(0x00); // byte 2
    _wire->endTransmission();
}
