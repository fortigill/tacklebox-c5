#pragma once

#include <Arduino.h>
#include <vector>

#include "hunt.h"

// BLE scanner (NimBLE-Arduino) with three modes:
//   Idle  - radio not scanning (default; Bluetooth is started on first use)
//   Scan  - one timed scan; found devices are collected (ignored ones skipped)
//   Hunt  - continuous scan tracking a single device's RSSI
namespace ble {

enum class Mode { Idle, Scan, Hunt, Rid };

// Receives Open Drone ID messages from BLE legacy adverts (service data
// 0xFFFA): one 25-byte message (or a message pack) per advert.
using RidSink = void (*)(const uint8_t *msg, size_t len, const char *addr, int rssi);

// Location trackers recognised from their advertisements
enum class Tracker : uint8_t { None, AppleFindMy, Tile, SmartTag, GoogleFindMy, Dult };

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
  Tracker tracker = Tracker::None;
  uint32_t firstSeen = 0;
  uint32_t lastSeen = 0;
  uint32_t seenCount = 0;
};

// A tracker seen across scans (kept between scans, unlike Device)
struct TrackerSighting {
  String addr;
  Tracker type = Tracker::None;
  int rssi = 0;
  uint32_t firstSeen = 0;
  uint32_t lastSeen = 0;
  uint32_t rounds = 0;      // number of separate scans it showed up in
  uint32_t lastRound = 0;
  bool alerted = false;     // "following" alert already shown
};

struct IgnoredDevice {
  String addr;
  String name;
};

const uint32_t SCAN_SECS = 8;
// "Following you": seen for at least this long, in at least this many scans
const uint32_t FOLLOW_MIN_MS = 10UL * 60 * 1000;
const uint32_t FOLLOW_MIN_ROUNDS = 5;

// Scanning
bool startScan();            // clears previous results; false if Bluetooth failed
bool startHunt(const String &addr);
bool startRid(RidSink sink);  // continuous scan feeding drone Remote ID messages
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

// Trackers
std::vector<TrackerSighting> trackers();  // most persistent first
bool isFollowing(const TrackerSighting &t);
void markAlerted(const String &addr);
void clearTrackers();
const char *trackerName(Tracker t);

// Ignore list (saved in NVS)
void ignore(const String &addr, const String &name);
void unignore(const String &addr);
void clearIgnored();
size_t ignoredCount();
std::vector<IgnoredDevice> ignoredList();

const char *vendorName(uint16_t mfgId);  // nullptr if unknown

}  // namespace ble
