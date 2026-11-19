#pragma once

#include <stdint.h>

// Signal-hunting data shared by the BLE and Wi-Fi hunt modes. Raw RSSI
// readings arrive at very different rates (dozens of BLE adverts per second
// vs a few Wi-Fi scans), so they are averaged over fixed windows and each
// window becomes one sample.
struct HuntInfo {
  uint32_t samples = 0;  // increments once per window that had readings
  int rssi = 0;          // average of the last window
  float smooth = 0;      // exponential moving average of window averages
  uint32_t lastSeen = 0; // millis() of the last raw reading
};

struct HuntAccumulator {
  static constexpr uint32_t WINDOW_MS = 700;
  static constexpr float ALPHA = 0.5f;  // higher = reacts faster, noisier

  int32_t sum = 0;
  int count = 0;
  uint32_t lastSeen = 0;
  uint32_t windowStart = 0;

  void reset(uint32_t now) {
    sum = count = 0;
    windowStart = now;
  }
  void add(int rssi, uint32_t now) {
    sum += rssi;
    count++;
    lastSeen = now;
  }
  // Call regularly; closes the window and folds it into `h` when due.
  void tick(HuntInfo &h, uint32_t now) {
    if (now - windowStart < WINDOW_MS) return;
    windowStart = now;
    if (!count) return;
    int avg = sum / count;
    h.smooth = h.samples ? h.smooth + ALPHA * (avg - h.smooth) : (float)avg;
    h.rssi = avg;
    h.lastSeen = lastSeen;
    h.samples++;
    sum = count = 0;
  }
};
