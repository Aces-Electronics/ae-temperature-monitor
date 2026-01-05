#ifndef TMP102_H
#define TMP102_H

#include <Arduino.h>
#include <Wire.h>

class TMP102 {
public:
    TMP102(uint8_t addr = 0x48);
    bool begin(int sda, int scl);
    float readTemperature();
    void shutdown();
    void wakeup();

private:
    uint8_t _addr;
    TwoWire *_wire;
};

#endif // TMP102_H
