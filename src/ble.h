#pragma once

#include <Arduino.h>
#include <vector>

// BLE scanner: continuous active scan in short cycles, devices tracked by
// address, forgotten after 60 s without an advertisement.
namespace ble {

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

bool start();      // initializes Bluetooth on first use; false if that failed
void stop();
bool scanning();
void loop();       // call from loop()

std::vector<Device> snapshot();  // sorted strongest first
bool find(const String &addr, Device &out);
const char *vendorName(uint16_t mfgId);  // nullptr if unknown

}  // namespace ble
