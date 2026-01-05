#include "neopixel.h"

StatusLed::StatusLed(int powerPin, int dataPin)
    : _powerPin(powerPin), _dataPin(dataPin), _pixels(1, dataPin, NEO_GRB + NEO_KHZ800) {}

void StatusLed::begin() {
    pinMode(_powerPin, OUTPUT);
    digitalWrite(_powerPin, HIGH); // Turn on power to LED
    _pixels.begin();
    _pixels.clear();
    _pixels.show();
}

void StatusLed::set(uint8_t r, uint8_t g, uint8_t b) {
    digitalWrite(_powerPin, HIGH);
    _pixels.setPixelColor(0, _pixels.Color(r, g, b));
    _pixels.show();
}

void StatusLed::off() {
    _pixels.clear();
    _pixels.show();
    digitalWrite(_powerPin, LOW); // Cut power to simple LED
}

void StatusLed::flash(uint8_t r, uint8_t g, uint8_t b, int duration) {
    set(r, g, b);
    delay(duration);
    off();
}
