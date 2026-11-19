#include "net.h"

#include <Preferences.h>
#include <WiFi.h>

#include <algorithm>

namespace net {

static const uint32_t CONNECT_TIMEOUT_MS = 20000;

static Preferences prefs;
static std::vector<AccessPoint> aps;
static ScanState scanSt = ScanState::Idle;
static State st = State::Idle;

static String curSsid;
static String pendPass;
static bool pendSave = false;
static uint32_t connectStart = 0;

static String saved;  // cached copy of the SSID in NVS

static void saveCreds(const String &s, const String &p) {
  prefs.begin("wifi", false);
  prefs.putString("ssid", s);
  prefs.putString("pass", p);
  prefs.end();
  saved = s;
}

String savedSsid() { return saved; }

void begin() {
  WiFi.persistent(false);  // we store credentials ourselves
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  // Flicker fix: with modem sleep on, the radio wakes in bursts every beacon
  // interval. Those current spikes sag the CYD's 3.3 V rail and the backlight
  // visibly pulses. Keeping the radio awake makes the current draw steady.
  WiFi.setSleep(false);

  prefs.begin("wifi", true);
  String s = prefs.getString("ssid", "");
  String p = prefs.getString("pass", "");
  prefs.end();
  saved = s;

  if (s.length()) {
    Serial.printf("[net] auto-connecting to saved network '%s'\n", s.c_str());
    connect(s, p, false);
  }
}

void connect(const String &ssid, const String &pass, bool save) {
  WiFi.disconnect(false, false);
  curSsid = ssid;
  pendPass = pass;
  pendSave = save;
  WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);  // DHCP by default
  st = State::Connecting;
  connectStart = millis();
}

void cancel() {
  WiFi.disconnect(false, false);
  pendPass = "";
  st = State::Idle;
}

void forget() {
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.end();
  saved = "";
  cancel();
  curSsid = "";
}

void startScan() {
  // A scan can't run while the radio is mid-connect; abort that attempt.
  if (st == State::Connecting) cancel();
  WiFi.scanDelete();
  int r = WiFi.scanNetworks(true /*async*/, false /*show hidden*/);
  scanSt = (r == WIFI_SCAN_FAILED) ? ScanState::Failed : ScanState::Running;
}

static void collectScan(int n) {
  aps.clear();
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s.isEmpty()) continue;  // hidden network
    int32_t r = WiFi.RSSI(i);
    bool sec = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    auto it = std::find_if(aps.begin(), aps.end(),
                           [&](const AccessPoint &a) { return a.ssid == s; });
    if (it == aps.end()) {
      aps.push_back({s, r, sec});
    } else if (r > it->rssi) {
      it->rssi = r;  // same SSID from several APs: keep the strongest
    }
  }
  std::sort(aps.begin(), aps.end(),
            [](const AccessPoint &a, const AccessPoint &b) { return a.rssi > b.rssi; });
  WiFi.scanDelete();
}

void loop() {
  if (scanSt == ScanState::Running) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_FAILED) {
      scanSt = ScanState::Failed;
    } else if (n >= 0) {
      collectScan(n);
      scanSt = ScanState::Done;
    }
  }

  wl_status_t ws = WiFi.status();
  uint32_t elapsed = millis() - connectStart;

  switch (st) {
    case State::Connecting:
      if (ws == WL_CONNECTED) {
        st = State::Connected;
        if (pendSave) saveCreds(curSsid, pendPass);
        pendPass = "";
        Serial.printf("[net] connected to '%s', IP %s\n", curSsid.c_str(),
                      WiFi.localIP().toString().c_str());
      } else if (elapsed > CONNECT_TIMEOUT_MS ||
                 (elapsed > 2000 && ws == WL_CONNECT_FAILED)) {
        Serial.printf("[net] failed to connect to '%s' (status %d)\n", curSsid.c_str(), ws);
        WiFi.disconnect(false, false);
        pendPass = "";
        st = State::Failed;
      }
      break;
    case State::Connected:
      if (ws != WL_CONNECTED) st = State::Lost;  // auto-reconnect will keep trying
      break;
    case State::Lost:
      if (ws == WL_CONNECTED) st = State::Connected;
      break;
    default:
      break;
  }
}

ScanState scanState() { return scanSt; }
const std::vector<AccessPoint> &results() { return aps; }
State state() { return st; }
String ssid() { return curSsid; }
String ip() { return WiFi.localIP().toString(); }
int rssi() { return WiFi.RSSI(); }

int signalPercent(int32_t r) {
  int q = 2 * (r + 100);
  return q < 0 ? 0 : (q > 100 ? 100 : q);
}

}  // namespace net
