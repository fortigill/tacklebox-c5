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

bool start();          // pauses Wi-Fi, starts sniffing; false if Bluetooth failed
void stop();           // stops and reconnects Wi-Fi
bool running();
void loop();           // call from loop(): channel hopping, hunt windows, LED

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
