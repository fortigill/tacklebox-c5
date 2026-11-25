#pragma once

#include <stdint.h>

namespace display {

// Rotation values follow TFT_eSPI: 0 = portrait, 1 = landscape,
// 2 = portrait (flipped), 3 = landscape (flipped).
enum Rotation : uint8_t { PORTRAIT = 0, LANDSCAPE = 1, PORTRAIT_FLIP = 2, LANDSCAPE_FLIP = 3 };

// One calibration sample: where the target was drawn (screen coords in the
// current rotation) and the raw XPT2046 reading the stylus produced there.
struct CalPoint {
  int16_t sx, sy;
  int16_t rx, ry;
};

// Initializes TFT, touch and LVGL. Loads saved rotation and calibration.
void begin();

uint8_t rotation();
void setRotation(uint8_t r);  // applies immediately and saves to NVS

// Backlight brightness in percent (MIN_BRIGHTNESS..100). save=false applies
// without writing flash (use while a slider is being dragged).
const uint8_t MIN_BRIGHTNESS = 5;
uint8_t brightness();
void setBrightness(uint8_t pct, bool save);

// Screen on/off (backlight). While off, the first touch only wakes it.
bool screenOn();
void setScreenOn(bool on);

// Some CYD panels show every color inverted (dark looks light, blue looks
// orange). On by default: this board's panel needs it.
bool invertColors();
void setInvertColors(bool on);  // applies immediately and saves to NVS

// Some CYD panels have red and blue swapped.
bool swapRedBlue();
void setSwapRedBlue(bool on);  // applies immediately and saves to NVS

bool hasCalibration();
// Fits raw->screen mapping from the samples. Saves and applies it if the
// worst-case error is acceptable. maxErrPx receives the worst error.
bool calibrate(const CalPoint *pts, int n, float *maxErrPx);

// Raw touch reading (no calibration applied). Returns false if not pressed.
bool readRaw(int16_t &x, int16_t &y);

}  // namespace display
