#include "net.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "board.h"
#include "soc/soc_caps.h"
#include "version.h"

#include <algorithm>

#if BOARD_DUAL_BAND && !SOC_WIFI_SUPPORT_5G
#error "BOARD_DUAL_BAND is set but this chip has no 5 GHz radio"
#endif
#define NET_DUAL BOARD_DUAL_BAND

namespace net {

static const uint32_t CONNECT_TIMEOUT_MS = 20000;

static Preferences prefs;
static std::vector<AccessPoint> aps;
static std::vector<ApInfo> raw;

// Wi-Fi hunt state
static const uint32_t HUNT_SCAN_GAP_MS = 250;    // pause between scans
static const uint32_t HUNT_MS_PER_CHAN = 120;    // dwell time of each scan
static bool huntOn = false;
static bool huntScanRunning = false;
static uint8_t huntBssid[6];
static uint8_t huntChannel = 0;
static uint32_t huntNextScan = 0;
static HuntInfo huntInfo;
static HuntAccumulator huntAcc;
static bool isSuspended = false;
static ScanState scanSt = ScanState::Idle;
static State st = State::Idle;

static String curSsid;
static String curPass;   // password of curSsid (needed to re-join on another band)
static uint32_t rawAt = 0;  // when the last user scan finished
static String pendSsid;  // network of the last user-initiated connect
static String pendPass;
static bool pendSave = false;
static uint32_t connectStart = 0;

static String saved;  // cached copy of the SSID in NVS

#if NET_DUAL
// ---------- Band steering (dual-band boards) ----------
// Join the 5 GHz access point of the network when it's strong enough, else
// the strongest one on either band. While connected:
//  - on 5 GHz with a weak signal (averaged below WEAK_5G for WEAK_HOLD_MS),
//    look for a clearly stronger access point (usually the 2.4 GHz one);
//  - on 2.4 GHz, check every few minutes whether 5 GHz has become strong
//    enough (UPGRADE_5G) to move back;
//  - if the link is lost, rescan and join whatever is best now, instead of
//    retrying only the access point we were locked to.
// The thresholds leave a wide gap (-67 up, -80 down) so it doesn't flip-flop.
static const int32_t PREFER_5G_MIN = -72;  // pick 5 GHz when at least this strong
static const int32_t WEAK_5G = -80;        // on 5 GHz below this: look for better
static const int32_t UPGRADE_5G = -67;     // on 2.4 GHz: move up when 5 GHz is this strong
static const int32_t SWITCH_MARGIN = 5;    // dB a weak-5G switch must gain
static const uint32_t WEAK_HOLD_MS = 15000;
static const uint32_t WEAK_RETRY_MS = 60000;
static const uint32_t UPGRADE_EVERY_MS = 180000;
static const uint32_t LOST_RESCAN_MS = 8000;
static const uint32_t LOST_RETRY_MS = 30000;
static const uint32_t ROAM_DWELL_MS = 120;
static const uint32_t RAW_FRESH_MS = 60000;  // reuse a user scan this recent to pick the AP

enum class Roam : uint8_t { None, Connect, Weak, Upgrade, Lost };
static Roam roamScan = Roam::None;  // purpose of the running band-steering scan
static int32_t emaRssi = 0;
static uint32_t lastRssiSample = 0, weakSince = 0, lostSince = 0;
static uint32_t nextWeakScan = 0, nextUpgradeScan = 0, nextLostScan = 0;

struct Cand {
  bool ok = false;
  uint8_t bssid[6] = {0};
  uint8_t ch = 0;
  int32_t rssi = -127;
};

static void consider(Cand &c, const uint8_t *bssid, uint8_t ch, int32_t r) {
  if (c.ok && r <= c.rssi) return;
  c.ok = true;
  memcpy(c.bssid, bssid, 6);
  c.ch = ch;
  c.rssi = r;
}

static Cand pick(const Cand &b5, const Cand &b24) {
  if (b5.ok && b5.rssi >= PREFER_5G_MIN) return b5;
  if (b24.ok && (!b5.ok || b24.rssi >= b5.rssi)) return b24;
  return b5;
}

static void bestFromScan(int n, Cand &b5, Cand &b24) {
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) != curSsid) continue;
    uint8_t ch = WiFi.channel(i);
    consider(is5GHz(ch) ? b5 : b24, WiFi.BSSID(i), ch, WiFi.RSSI(i));
  }
}

static void beginOn(const Cand &c, const char *why) {
  const uint8_t *b = c.bssid;
  Serial.printf("[net] %s: joining '%s' on %s GHz ch %u (%d dBm, %02x:%02x:%02x:%02x:%02x:%02x)\n",
                why, curSsid.c_str(), is5GHz(c.ch) ? "5" : "2.4", c.ch, (int)c.rssi, b[0], b[1],
                b[2], b[3], b[4], b[5]);
  WiFi.disconnect(false, false);
  WiFi.begin(curSsid.c_str(), curPass.length() ? curPass.c_str() : nullptr, c.ch, c.bssid);
  st = State::Connecting;
  connectStart = millis();
}

static void beginAny(const char *why) {  // no scan result: let the radio choose
  Serial.printf("[net] %s: joining '%s' on any band\n", why, curSsid.c_str());
  WiFi.disconnect(false, false);
  WiFi.begin(curSsid.c_str(), curPass.length() ? curPass.c_str() : nullptr);
  st = State::Connecting;
  connectStart = millis();
}

static bool startRoamScan(Roam why) {
  // async, include hidden, active, short dwell, all channels, only this SSID
  int r = WiFi.scanNetworks(true, true, false, ROAM_DWELL_MS, 0, curSsid.c_str());
  if (r != WIFI_SCAN_RUNNING) {
    Serial.printf("[net] band scan could not start (%d)\n", r);
    return false;
  }
  roamScan = why;
  return true;
}

static void abortRoam() {
  if (roamScan == Roam::None) return;
  WiFi.scanDelete();
  roamScan = Roam::None;
}

static void roamOnConnected() {
  emaRssi = 0;
  weakSince = 0;
  lostSince = 0;
  nextUpgradeScan = millis() + UPGRADE_EVERY_MS;
}

static void roamLoop() {
  uint32_t now = millis();
  if (roamScan != Roam::None) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    Roam why = roamScan;
    roamScan = Roam::None;
    Cand b5, b24;
    if (n > 0) bestFromScan(n, b5, b24);
    if (n >= 0) WiFi.scanDelete();
    Cand best = pick(b5, b24);
    Serial.printf("[net] band scan: 5 GHz %d dBm, 2.4 GHz %d dBm\n", b5.ok ? (int)b5.rssi : 0,
                  b24.ok ? (int)b24.rssi : 0);
    switch (why) {
      case Roam::Connect:
      case Roam::Lost:
        if (best.ok) beginOn(best, why == Roam::Connect ? "connect" : "reconnect");
        else beginAny(why == Roam::Connect ? "connect" : "reconnect");
        break;
      case Roam::Weak:
        if (st == State::Connected && best.ok && memcmp(best.bssid, WiFi.BSSID(), 6) != 0 &&
            best.rssi >= emaRssi + SWITCH_MARGIN)
          beginOn(best, "5 GHz signal weak");
        break;
      case Roam::Upgrade:
        if (st == State::Connected && b5.ok && b5.rssi >= UPGRADE_5G &&
            memcmp(b5.bssid, WiFi.BSSID(), 6) != 0)
          beginOn(b5, "5 GHz strong again");
        break;
      default:
        break;
    }
    return;
  }

  if (st == State::Connected) {
    lostSince = 0;
    if (now - lastRssiSample >= 1000) {
      lastRssiSample = now;
      int32_t r = WiFi.RSSI();
      if (r < 0) emaRssi = emaRssi ? (emaRssi * 3 + r) / 4 : r;
    }
    if (is5GHz(WiFi.channel())) {
      if (emaRssi && emaRssi < WEAK_5G) {
        if (!weakSince) weakSince = now;
        if (now - weakSince >= WEAK_HOLD_MS && (int32_t)(now - nextWeakScan) >= 0) {
          nextWeakScan = now + WEAK_RETRY_MS;
          startRoamScan(Roam::Weak);
        }
      } else {
        weakSince = 0;
      }
    } else if ((int32_t)(now - nextUpgradeScan) >= 0) {
      nextUpgradeScan = now + UPGRADE_EVERY_MS;
      startRoamScan(Roam::Upgrade);
    }
  } else if (curSsid.length() &&
             (st == State::Lost || (st == State::Failed && curSsid == saved))) {
    // Known-good credentials but no link: look again on both bands
    if (!lostSince) lostSince = now;
    if (now - lostSince >= LOST_RESCAN_MS && (int32_t)(now - nextLostScan) >= 0) {
      nextLostScan = now + LOST_RETRY_MS;
      WiFi.disconnect(false, false);  // stop retrying the old access point so we can scan
      if (!startRoamScan(Roam::Lost)) beginAny("reconnect");
    }
  } else {
    lostSince = 0;
  }
}
#endif  // NET_DUAL

static void setBands() {
#if NET_DUAL
  esp_err_t e = esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);  // scan and join on 2.4 + 5 GHz
  Serial.printf("[net] dual-band mode %s\n", e == ESP_OK ? "on" : esp_err_to_name(e));
#endif
}

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
  WiFi.setHostname(PRODUCT_HOSTNAME);  // shows up by name in the router's client list
  WiFi.mode(WIFI_STA);
  setBands();
  WiFi.setAutoReconnect(true);
  // Flicker fix: with modem sleep on, the radio wakes in bursts every beacon
  // interval. Those current spikes sag the CYD's 3.3 V rail and the backlight
  // visibly pulses. Keeping the radio awake makes the current draw steady.
  WiFi.setSleep(false);

  prefs.begin("wifi", false);  // read-write: creates the namespace on first boot
  String s = prefs.isKey("ssid") ? prefs.getString("ssid") : String();
  String p = prefs.isKey("pass") ? prefs.getString("pass") : String();
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
  curPass = pass;
  pendSsid = ssid;
  pendPass = pass;
  pendSave = save;
  st = State::Connecting;
  connectStart = millis();
#if NET_DUAL
  abortRoam();
  // Pick the band/access point: from a recent scan if we have one (the user
  // just tapped the network in the list), otherwise with a quick scan first.
  if (rawAt && millis() - rawAt < RAW_FRESH_MS) {
    Cand b5, b24;
    for (const auto &a : raw)
      if (a.ssid == ssid) consider(is5GHz(a.channel) ? b5 : b24, a.bssid, a.channel, a.rssi);
    if (b5.ok || b24.ok) {
      beginOn(pick(b5, b24), "connect");
      return;
    }
  }
  if (startRoamScan(Roam::Connect)) return;
#endif
  WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);  // DHCP by default
}

void cancel() {
#if NET_DUAL
  abortRoam();
#endif
  WiFi.disconnect(false, false);
  pendPass = "";
  pendSave = false;
  st = State::Idle;
}

void forget() {
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.end();
  saved = "";
  cancel();
  curSsid = "";
  curPass = "";
}

void startScan(bool includeHidden) {
  if (isSuspended) {  // radio is busy sniffing
    scanSt = ScanState::Failed;
    return;
  }
  // A scan can't run while the radio is mid-connect; abort that attempt.
  if (st == State::Connecting) cancel();
#if NET_DUAL
  abortRoam();  // the user's scan wins over a background band check
#endif
  stopHunt();
  WiFi.scanDelete();
  // Dual band has ~3x the channels; a shorter dwell keeps the scan near 6 s
  int r = WiFi.scanNetworks(true /*async*/, includeHidden, false, NET_DUAL ? 150 : 300);
  scanSt = (r == WIFI_SCAN_FAILED) ? ScanState::Failed : ScanState::Running;
}

static void collectScan(int n) {
  aps.clear();
  raw.clear();
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    int32_t r = WiFi.RSSI(i);
    bool sec = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;

    ApInfo ai;
    ai.ssid = s;
    memcpy(ai.bssid, WiFi.BSSID(i), 6);
    ai.bssidStr = WiFi.BSSIDstr(i);
    ai.rssi = r;
    ai.channel = WiFi.channel(i);
    ai.secure = sec;
    raw.push_back(ai);

    if (s.isEmpty()) continue;  // hidden network: analyzer only
    auto it = std::find_if(aps.begin(), aps.end(),
                           [&](const AccessPoint &a) { return a.ssid == s; });
    uint8_t band = is5GHz(ai.channel) ? BAND_5 : BAND_24;
    if (it == aps.end()) {
      aps.push_back({s, r, sec, band});
    } else {
      it->bands |= band;
      if (r > it->rssi) it->rssi = r;  // same SSID from several APs: keep the strongest
    }
  }
  std::sort(aps.begin(), aps.end(),
            [](const AccessPoint &a, const AccessPoint &b) { return a.rssi > b.rssi; });
  std::sort(raw.begin(), raw.end(),
            [](const ApInfo &a, const ApInfo &b) { return a.rssi > b.rssi; });
  WiFi.scanDelete();
  rawAt = millis();
}

void startHunt(const uint8_t bssid[6], uint8_t channel) {
  if (isSuspended) return;
  if (st == State::Connecting) cancel();
#if NET_DUAL
  abortRoam();
#endif
  if (scanSt == ScanState::Running) {  // let a running scan's results be dropped
    WiFi.scanDelete();
    scanSt = ScanState::Idle;
  }
  memcpy(huntBssid, bssid, 6);
  huntChannel = channel;
  huntInfo = HuntInfo();
  huntAcc.reset(millis());
  huntScanRunning = false;
  huntNextScan = 0;
  huntOn = true;
}

void stopHunt() {
  if (!huntOn) return;
  huntOn = false;
  if (huntScanRunning) WiFi.scanDelete();
  huntScanRunning = false;
}

bool hunting() { return huntOn; }

void suspend() {
  if (isSuspended) return;
  isSuspended = true;
  stopHunt();
#if NET_DUAL
  abortRoam();
#endif
  if (scanSt == ScanState::Running) {
    WiFi.scanDelete();
    scanSt = ScanState::Idle;
  }
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);
  st = State::Idle;
  Serial.println("[net] suspended for radio sniffing");
}

void resume() {
  if (!isSuspended) return;
  isSuspended = false;
  setBands();  // the sniffer may have changed the band
  WiFi.setAutoReconnect(true);
  prefs.begin("wifi", false);
  String s = prefs.isKey("ssid") ? prefs.getString("ssid") : String();
  String p = prefs.isKey("pass") ? prefs.getString("pass") : String();
  prefs.end();
  if (s.length()) {
    Serial.printf("[net] resuming, reconnecting to '%s'\n", s.c_str());
    connect(s, p, false);
  }
}

bool suspended() { return isSuspended; }
HuntInfo hunt() { return huntInfo; }

static void huntLoop() {
  uint32_t now = millis();
  if (huntScanRunning) {
    int n = WiFi.scanComplete();
    if (n >= 0) {
      for (int i = 0; i < n; i++) {
        if (memcmp(WiFi.BSSID(i), huntBssid, 6) == 0) huntAcc.add(WiFi.RSSI(i), now);
      }
      WiFi.scanDelete();
      huntScanRunning = false;
      huntNextScan = now + HUNT_SCAN_GAP_MS;
    } else if (n == WIFI_SCAN_FAILED) {
      huntScanRunning = false;
      huntNextScan = now + HUNT_SCAN_GAP_MS;
    }
  } else if ((int32_t)(now - huntNextScan) >= 0) {
    uint8_t b[6];
    memcpy(b, huntBssid, 6);
    // async, include hidden, active, short dwell, one channel, filter by BSSID
    int r = WiFi.scanNetworks(true, true, false, HUNT_MS_PER_CHAN, huntChannel, nullptr, b);
    huntScanRunning = (r == WIFI_SCAN_RUNNING);
    if (!huntScanRunning) huntNextScan = now + HUNT_SCAN_GAP_MS;
  }
  huntAcc.tick(huntInfo, now);
}

void loop() {
  if (isSuspended) return;  // radio is in use by the sniffer
  if (huntOn) huntLoop();
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
  State prev = st;

  // The radio's real link state wins over our bookkeeping: the Arduino 3.x
  // core can finish joining after we've given up (auto-reconnect), so an
  // Idle/Failed/Lost state with a live link means we're connected.
  // Right after a (re)join the old link can still read as connected for a
  // moment; a real join takes well over a second.
  bool justStarted = st == State::Connecting && elapsed < 800;
  if (ws == WL_CONNECTED && st != State::Connected && !justStarted) {
    st = State::Connected;
    String actual = WiFi.SSID();
    if (actual.length()) curSsid = actual;
    if (pendSave && curSsid == pendSsid) saveCreds(curSsid, pendPass);
    pendSave = false;
    pendPass = "";
  } else if (st == State::Connecting &&
#if NET_DUAL
             roamScan != Roam::Connect &&  // still choosing the access point
#endif
             (elapsed > CONNECT_TIMEOUT_MS || (elapsed > 2000 && ws == WL_CONNECT_FAILED))) {
    Serial.printf("[net] failed to connect to '%s' (status %d)\n", curSsid.c_str(), ws);
    WiFi.disconnect(false, false);
    st = State::Failed;
    // keep pendSave/pendPass: if the core still joins, we save then
  } else if (st == State::Connected && ws != WL_CONNECTED) {
    st = State::Lost;  // auto-reconnect will keep trying
  }

  if (st != prev) {
    static const char *names[] = {"Idle", "Connecting", "Connected", "Lost", "Failed"};
    Serial.printf("[net] state %s -> %s (wifi status %d)", names[(int)prev], names[(int)st], ws);
    if (st == State::Connected)
      Serial.printf(", '%s' IP %s, ch %d (%s GHz) %d dBm", curSsid.c_str(),
                    WiFi.localIP().toString().c_str(), (int)WiFi.channel(),
                    is5GHz(WiFi.channel()) ? "5" : "2.4", (int)WiFi.RSSI());
    Serial.println();
#if NET_DUAL
    if (st == State::Connected) roamOnConnected();
#endif
  }

#if NET_DUAL
  if (!huntOn && scanSt != ScanState::Running) roamLoop();
#endif
}

ScanState scanState() { return scanSt; }
const std::vector<AccessPoint> &results() { return aps; }
const std::vector<ApInfo> &rawResults() { return raw; }
State state() { return st; }
String ssid() { return curSsid; }
String ip() { return WiFi.localIP().toString(); }
int rssi() { return WiFi.RSSI(); }
int channel() { return st == State::Connected ? (int)WiFi.channel() : 0; }
int band() {
  int c = channel();
  return c == 0 ? 0 : (is5GHz(c) ? 5 : 2);
}
bool dualBand() { return NET_DUAL; }

int signalPercent(int32_t r) {
  int q = 2 * (r + 100);
  return q < 0 ? 0 : (q > 100 ? 100 : q);
}

}  // namespace net
