#pragma once

#include <stdint.h>

// The RGB LED on the back of the CYD (common anode: a pin driven LOW lights
// that color). Blinking runs on an esp_timer, so it keeps going while the
// main loop is busy (e.g. during setup()).
namespace led {

enum class Color : uint8_t { Off, Red, Green, Blue, Yellow, Cyan, Magenta, White };

void begin();                                  // pins as outputs, LED off
void set(Color c);                             // solid color (stops blinking)
void blink(Color c, uint32_t halfPeriodMs);    // on/off every halfPeriodMs

}  // namespace led
