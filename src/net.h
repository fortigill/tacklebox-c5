#pragma once

#include <Arduino.h>
#include <vector>

#include "hunt.h"

// Wi-Fi manager: async scanning, DHCP connect, credentials saved in NVS.
namespace net {

struct AccessPoint {
  String ssid;
  int32_t rssi;
  bool secure;
};

// One access point as the analyzer sees it (not deduplicated by SSID)
struct ApInfo {
  String ssid;      // empty for hidden networks
  uint8_t bssid[6];
  String bssidStr;
  int32_t rssi;
  uint8_t channel;
  bool secure;
};

enum class State { Idle, Connecting, Connected, Lost, Failed };
enum class ScanState { Idle, Running, Done, Failed };

void begin();   // loads saved credentials and auto-connects
void loop();    // call from loop()

void startScan(bool includeHidden = false);
ScanState scanState();
const std::vector<AccessPoint> &results();  // sorted strongest first, deduped
const std::vector<ApInfo> &rawResults();    // every AP, strongest first

// Wi-Fi hunt: repeated single-channel scans tracking one access point
void startHunt(const uint8_t bssid[6], uint8_t channel);
void stopHunt();
bool hunting();
HuntInfo hunt();

void connect(const String &ssid, const String &pass, bool save);
void cancel();
void forget();  // clears saved credentials and disconnects

State state();
String ssid();       // network we're connected/connecting to
String savedSsid();  // network stored in NVS ("" if none)
String ip();
int rssi();

int signalPercent(int32_t rssi);

}  // namespace net
