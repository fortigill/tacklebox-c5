#pragma once

#include <Arduino.h>
#include <vector>

#include "hunt.h"

// Wi-Fi manager: async scanning, DHCP connect, credentials saved in NVS.
namespace net {

// Bands an SSID was seen on (AccessPoint::bands)
const uint8_t BAND_24 = 1;
const uint8_t BAND_5 = 2;

inline bool is5GHz(uint8_t channel) { return channel > 14; }

struct AccessPoint {
  String ssid;
  int32_t rssi;    // strongest access point of this SSID
  bool secure;
  uint8_t bands;   // BAND_24 | BAND_5
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

// Drop the station connection so the radio can be used for sniffing, and
// bring it back afterwards (reconnects to the saved network).
void suspend();
void resume();
bool suspended();

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
int channel();  // channel of the current connection, 0 if none
int band();     // 2 or 5 (GHz) while connected, else 0
bool dualBand();  // this board's radio does 5 GHz too

int signalPercent(int32_t rssi);

}  // namespace net
