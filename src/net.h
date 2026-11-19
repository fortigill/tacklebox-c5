#pragma once

#include <Arduino.h>
#include <vector>

// Wi-Fi manager: async scanning, DHCP connect, credentials saved in NVS.
namespace net {

struct AccessPoint {
  String ssid;
  int32_t rssi;
  bool secure;
};

enum class State { Idle, Connecting, Connected, Lost, Failed };
enum class ScanState { Idle, Running, Done, Failed };

void begin();   // loads saved credentials and auto-connects
void loop();    // call from loop()

void startScan();
ScanState scanState();
const std::vector<AccessPoint> &results();  // sorted strongest first, deduped

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
