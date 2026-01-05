#ifndef NEOPIXEL_H
#define NEOPIXEL_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

class StatusLed {
public:
    StatusLed(int powerPin, int dataPin);
    void begin();
    void set(uint8_t r, uint8_t g, uint8_t b);
    void off();
    void flash(uint8_t r, uint8_t g, uint8_t b, int duration = 100);

private:
    int _powerPin;
    int _dataPin;
    Adafruit_NeoPixel _pixels;
};

#endif // NEOPIXEL_H
