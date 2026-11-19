#pragma once

#include <Arduino.h>
#include <vector>

// BLE scanner with three modes:
//   Idle  - radio not scanning (default; Bluetooth is started on first use)
//   Scan  - one timed scan; found devices are collected (ignored ones skipped)
//   Hunt  - track a single device's RSSI with 1 s scan cycles
namespace ble {

enum class Mode { Idle, Scan, Hunt };

struct Device {
  String addr;
  String name;
  int rssi = 0;
  bool randomAddr = false;
  bool hasMfg = false;
  uint16_t mfgId = 0;
  bool hasTx = false;
  int8_t tx = 0;
  String service;
  uint32_t firstSeen = 0;
  uint32_t lastSeen = 0;
  uint32_t seenCount = 0;
};

struct HuntInfo {
  uint32_t samples = 0;  // increments with every report from the target
  int rssi = 0;          // last raw reading
  float smooth = 0;      // smoothed RSSI (exponential moving average)
  uint32_t lastSeen = 0;
};

struct IgnoredDevice {
  String addr;
  String name;
};

const uint32_t SCAN_SECS = 8;

// Scanning
bool startScan();            // clears previous results; false if Bluetooth failed
bool startHunt(const String &addr);
void stop();
Mode mode();
uint32_t scanRemainingMs();  // during Scan
bool everScanned();
void loop();                 // call from loop()

// Results
size_t count();
std::vector<Device> snapshot(size_t maxCount, size_t *total);  // strongest first
bool find(const String &addr, Device &out);
HuntInfo hunt();

// Ignore list (saved in NVS)
void ignore(const String &addr, const String &name);
void unignore(const String &addr);
void clearIgnored();
size_t ignoredCount();
std::vector<IgnoredDevice> ignoredList();

const char *vendorName(uint16_t mfgId);  // nullptr if unknown

}  // namespace ble
