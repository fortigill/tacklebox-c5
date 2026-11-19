#pragma once

#include <Arduino.h>
#include <vector>

#include "hunt.h"

// Drone Remote ID receiver (ASTM F3411 / ASD-STAN EN 4709-002 "Open Drone ID").
// Listens on:
//   - Wi-Fi beacons with the Remote ID vendor IE (DJI, Skydio, Parrot, add-on modules)
//   - Wi-Fi NaN service discovery frames (channel 6)
//   - Bluetooth 4 legacy advertisements (service data 0xFFFA)
// Wi-Fi is put in promiscuous mode and hops channels, so the station
// connection is paused while the scanner runs and restored on stop.
namespace drone {

enum class Transport : uint8_t { Ble, WifiBeacon, WifiNan };

struct Drone {
  String key;          // transmitter MAC
  Transport transport = Transport::WifiBeacon;
  uint8_t channel = 0; // Wi-Fi channel (0 for BLE)
  String ssid;         // beacon SSID, if any

  // Basic ID
  String uasId;
  uint8_t idType = 0;  // 1 serial, 2 CAA registration, 3 UTM UUID, 4 session ID
  uint8_t uaType = 0;

  // Location
  bool hasLoc = false;
  uint8_t status = 0;  // 0 undeclared, 1 ground, 2 airborne, 3 emergency, 4 RID failure
  double lat = 0, lon = 0;
  float altGeo = NAN, altBaro = NAN, height = NAN;  // meters
  float speed = NAN, vspeed = NAN;                  // m/s
  float heading = NAN;                              // degrees

  // System / operator
  bool hasOp = false;
  double opLat = 0, opLon = 0;
  String operatorId;
  String selfId;

  int rssi = 0;
  uint32_t firstSeen = 0, lastSeen = 0;
  uint32_t messages = 0;
};

// Scan  = continuous until stopped.
// Watch = repeating cycle: scan WATCH_SCAN_MS, rest WATCH_REST_MS (Wi-Fi is
//         reconnected while resting). Every drone heard goes into the watch log.
enum class Mode { Off, Scan, Watch };
const uint32_t WATCH_SCAN_MS = 5000;
const uint32_t WATCH_REST_MS = 25000;
// A watch scan keeps going while Remote ID is being heard, and only rests once
// nothing has been heard for this long (gives slow/weak drones time to deliver
// every message type).
const uint32_t WATCH_LINGER_MS = 8000;

void begin();          // loads the watch log from NVS
bool startScan();      // pauses Wi-Fi, starts sniffing
bool startWatch();
void stop();           // stops either mode and reconnects Wi-Fi
Mode mode();
bool running();        // any mode active (radio tools stay blocked)
bool radioActive();    // radios currently sniffing (false while Watch rests)
uint32_t phaseRemainingMs();  // Watch: time left in the current scan/rest phase
bool watchExtended();         // Watch scan running past 5 s because a drone is in range
void loop();           // call from loop(): watch cycle, channel hopping, hunt, LED

// Watch log: one entry per drone ever heard in Watch mode, kept in NVS until
// deleted. Fixed-size record so the whole log is a single NVS blob.
enum : uint8_t { LOG_HAS_LOC = 1, LOG_HAS_OP = 2, LOG_DJI = 4, LOG_EMERGENCY = 8 };
struct LogEntry {
  char key[18];        // transmitter MAC (identifies the entry)
  char id[21];         // Remote ID (UAS ID)
  uint8_t idType, uaType, transport, flags;
  int8_t rssiMax, rssiLast;
  uint16_t sightings;  // number of watch scans it was heard in
  uint32_t first, last;  // epoch seconds (0 = clock wasn't set)
  int32_t lat, lon;      // last drone position, 1e-7 degrees
  int32_t opLat, opLon;  // last operator position
  int16_t alt;           // last geodetic altitude, meters (INT16_MIN = unknown)
  char opId[21];         // Operator ID, if the drone broadcasts one
  char selfId[24];       // pilot's description text (Self-ID), if any
};

std::vector<LogEntry> watchLog();  // most recently heard first
bool logFind(const String &keyOrId, LogEntry &out);  // by MAC or Remote ID
void logDelete(const String &key);
void logClear();
size_t logCount();
uint32_t logNewCount();      // increments each time a new drone is logged
String logNewestTitle();

// Watch alerts. Each watch scan queues the drones it heard, except those
// already queued or acknowledged. Drones are identified by Remote ID (falling
// back to MAC), since some transmitters rotate their Bluetooth address.
// Closing or viewing an alert acknowledges it: no repeats while the drone
// stays around; it alerts again once it has been gone ACK_GONE_MS, when Watch
// is restarted, or after it is deleted from the log.
const uint32_t ACK_GONE_MS = 2 * (WATCH_SCAN_MS + WATCH_REST_MS) + 5000;
bool nextAlert(String &id);              // pop the next drone to alert about
void acknowledge(const String &id);
bool watchDroneNearby();                 // Watch heard a drone within the last cycle

std::vector<Drone> list();  // most recently heard first
bool find(const String &key, Drone &out);
uint8_t currentChannel();
uint32_t frames();          // Remote ID messages received since start

void setHuntTarget(const String &key);  // "" to stop
HuntInfo hunt();

const uint32_t STALE_MS = 30000;  // greyed out after this long without a message

bool isDji(const Drone &d);
const char *uaTypeName(uint8_t t);
const char *statusName(uint8_t s);
const char *idTypeName(uint8_t t);
const char *transportName(Transport t);
float distanceM(double lat1, double lon1, double lat2, double lon2);

}  // namespace drone
