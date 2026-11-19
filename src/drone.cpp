#include "drone.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <math.h>
#include <time.h>

#include <algorithm>
#include <mutex>

#include "ble.h"
#include "led.h"
#include "net.h"

namespace drone {
namespace {

const size_t MAX_DRONES = 20;
const size_t ODID_MSG_SIZE = 25;
const uint32_t HOP_DWELL_MS = 110;
// Every other stop is channel 6 (NaN discovery channel and a common beacon
// channel); the rest sweep 1-13.
const uint8_t HOP_SEQ[] = {6, 1, 6, 2, 6, 3, 6, 4, 6, 5, 6, 7, 6, 8, 6, 9, 6, 10, 6, 11, 6, 12, 6, 13};
const size_t HOP_LEN = sizeof(HOP_SEQ);

// Vendor IE OUIs used for Remote ID in Wi-Fi beacons (vendor type 0x0D)
const uint8_t OUI_ASTM[3] = {0xFA, 0x0B, 0xBC};
const uint8_t OUI_CEN[3] = {0x90, 0x3A, 0xE6};
// Wi-Fi Alliance OUI + NaN type, and the NaN service ID of
// "org.opendroneid.remoteid"
const uint8_t OUI_WFA[3] = {0x50, 0x6F, 0x9A};
const uint8_t NAN_SERVICE_ID[6] = {0x88, 0x69, 0x19, 0x9D, 0x92, 0x09};

std::mutex mtx;  // fed from the Wi-Fi task and the NimBLE host task
std::vector<Drone> drones;
volatile bool active = false;
uint32_t frameCount = 0;  // written under mtx
size_t hopIdx = 0;
uint32_t nextHop = 0;
uint8_t channel = 6;

String huntKey;
uint8_t huntChannel = 0;  // while hunting a Wi-Fi drone, stay on its channel
HuntInfo huntInfo;
HuntAccumulator huntAcc;
int ledState = -1;

// ---------- Open Drone ID decoding ----------

uint16_t u16(const uint8_t *p) { return p[0] | (p[1] << 8); }
int32_t i32(const uint8_t *p) { return (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }

float altitude(const uint8_t *p) {  // 0.5 m resolution, offset -1000 m; 0 = unknown
  uint16_t v = u16(p);
  return v ? v * 0.5f - 1000.0f : NAN;
}

String text(const uint8_t *p, size_t n) {
  char buf[32];
  size_t k = 0;
  for (size_t i = 0; i < n && k < sizeof(buf) - 1; i++) {
    if (!p[i]) break;
    buf[k++] = (p[i] >= 0x20 && p[i] < 0x7F) ? (char)p[i] : '?';
  }
  buf[k] = 0;
  String s(buf);
  s.trim();
  return s;
}

void decodeMessage(Drone &d, const uint8_t *m);

void decodePack(Drone &d, const uint8_t *m, size_t len) {
  if (len < 3) return;
  size_t size = m[1], n = m[2];
  if (size != ODID_MSG_SIZE) return;
  for (size_t i = 0; i < n && 3 + (i + 1) * size <= len; i++) {
    const uint8_t *sub = m + 3 + i * size;
    if ((sub[0] >> 4) != 0xF) decodeMessage(d, sub);  // no nested packs
  }
}

void decodeMessage(Drone &d, const uint8_t *m) {
  uint8_t type = m[0] >> 4;
  switch (type) {
    case 0x0: {  // Basic ID
      uint8_t idt = m[1] >> 4;
      String id = text(m + 2, 20);
      if (id.isEmpty()) break;
      // A drone can send several Basic IDs; prefer the serial number
      if (d.uasId.isEmpty() || idt == 1 || d.idType != 1) {
        d.uasId = id;
        d.idType = idt;
      }
      d.uaType = m[1] & 0x0F;
      break;
    }
    case 0x1: {  // Location / vector
      uint8_t f = m[1];
      d.status = f >> 4;
      bool ew = f & 0x02, mult = f & 0x01;
      int dir = m[2] + (ew ? 180 : 0);
      d.heading = dir <= 360 ? (float)dir : NAN;
      d.speed = m[3] == 255 ? NAN : (mult ? m[3] * 0.75f + 63.75f : m[3] * 0.25f);
      int8_t vs = (int8_t)m[4];
      d.vspeed = vs == 63 ? NAN : vs * 0.5f;
      int32_t la = i32(m + 5), lo = i32(m + 9);
      if (la || lo) {
        d.lat = la * 1e-7;
        d.lon = lo * 1e-7;
        d.hasLoc = fabs(d.lat) <= 90 && fabs(d.lon) <= 180;
      }
      d.altBaro = altitude(m + 13);
      d.altGeo = altitude(m + 15);
      d.height = altitude(m + 17);
      break;
    }
    case 0x3:  // Self-ID (free text, e.g. purpose of flight)
      d.selfId = text(m + 2, 23);
      break;
    case 0x4: {  // System: operator location
      int32_t la = i32(m + 2), lo = i32(m + 6);
      if (la || lo) {
        d.opLat = la * 1e-7;
        d.opLon = lo * 1e-7;
        d.hasOp = fabs(d.opLat) <= 90 && fabs(d.opLon) <= 180;
      }
      break;
    }
    case 0x5:  // Operator ID
      d.operatorId = text(m + 2, 20);
      break;
    case 0xF:
      break;  // packs are handled by decodePack
    default:
      break;  // 0x2 authentication: not decoded
  }
}

// Find or create the record for a transmitter and apply a decoder to it.
template <typename F>
void withDrone(const uint8_t *mac, const char *macStr, Transport t, uint8_t ch, int rssi, F apply) {
  char keyBuf[18];
  if (!macStr) {
    snprintf(keyBuf, sizeof(keyBuf), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
    macStr = keyBuf;
  }
  uint32_t now = millis();
  std::lock_guard<std::mutex> lock(mtx);
  if (!active) return;
  auto it = std::find_if(drones.begin(), drones.end(), [&](const Drone &d) { return d.key == macStr; });
  if (it == drones.end()) {
    if (drones.size() >= MAX_DRONES) {  // replace the stalest
      it = std::min_element(drones.begin(), drones.end(),
                            [](const Drone &a, const Drone &b) { return a.lastSeen < b.lastSeen; });
      *it = Drone();
    } else {
      drones.push_back(Drone());
      it = drones.end() - 1;
    }
    it->key = macStr;
    it->firstSeen = now;
    Serial.printf("[rid] new transmitter %s via %s\n", macStr, transportName(t));
  }
  it->transport = t;
  it->channel = ch;
  it->rssi = rssi;
  it->lastSeen = now;
  it->messages++;
  frameCount++;
  apply(*it);
  if (huntKey.length() && it->key == huntKey) huntAcc.add(rssi, now);
}

// ---------- Wi-Fi sniffer ----------

void parseBeacon(const uint8_t *f, int len, int rssi, uint8_t ch) {
  const uint8_t *src = f + 10;
  String ssid;
  const uint8_t *pack = nullptr;
  size_t packLen = 0;
  for (int i = 36; i + 2 <= len;) {  // 24-byte header + 12 fixed bytes, then IEs
    uint8_t id = f[i], l = f[i + 1];
    const uint8_t *v = f + i + 2;
    if (i + 2 + l > len) break;
    if (id == 0 && l <= 32) ssid = text(v, l);
    if (id == 0xDD && l >= 6 && v[3] == 0x0D &&
        (memcmp(v, OUI_ASTM, 3) == 0 || memcmp(v, OUI_CEN, 3) == 0)) {
      pack = v + 5;  // after OUI, vendor type and message counter
      packLen = l - 5;
    }
    i += 2 + l;
  }
  if (!pack) return;
  withDrone(src, nullptr, Transport::WifiBeacon, ch, rssi, [&](Drone &d) {
    if (ssid.length()) d.ssid = ssid;
    if ((pack[0] >> 4) == 0xF) decodePack(d, pack, packLen);
    else if (packLen >= ODID_MSG_SIZE) decodeMessage(d, pack);
  });
}

void parseNan(const uint8_t *f, int len, int rssi, uint8_t ch) {
  // Public action (0x04), vendor specific (0x09), WFA OUI, NaN type 0x13
  if (len < 30 || f[24] != 0x04 || f[25] != 0x09 || memcmp(f + 26, OUI_WFA, 3) != 0 || f[29] != 0x13)
    return;
  const uint8_t *src = f + 10;
  for (int i = 30; i + 3 <= len;) {  // NaN attributes: id(1) len(2) body
    uint8_t id = f[i];
    uint16_t l = f[i + 1] | (f[i + 2] << 8);
    const uint8_t *a = f + i + 3;
    if (i + 3 + l > len) break;
    if (id == 0x03 && l >= 9 && memcmp(a, NAN_SERVICE_ID, 6) == 0) {  // Service Descriptor
      uint8_t ctrl = a[8];
      size_t p = 9;
      if (ctrl & 0x20) p += 2;                    // binding bitmap
      if ((ctrl & 0x04) && p < l) p += 1 + a[p];  // matching filter
      if ((ctrl & 0x08) && p < l) p += 1 + a[p];  // service response filter
      if ((ctrl & 0x10) && p < l) {               // service info
        size_t infoLen = a[p];
        const uint8_t *info = a + p + 1;
        if (p + 1 + infoLen <= l && infoLen > 1) {
          const uint8_t *pack = info + 1;  // skip message counter
          size_t packLen = infoLen - 1;
          withDrone(src, nullptr, Transport::WifiNan, ch, rssi,
                    [&](Drone &d) { decodePack(d, pack, packLen); });
        }
      }
    }
    i += 3 + l;
  }
}

void snifferCb(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT || !active) return;
  const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
  int len = pkt->rx_ctrl.sig_len - 4;  // drop FCS
  if (len < 24) return;
  const uint8_t *f = pkt->payload;
  if (f[0] == 0x80) parseBeacon(f, len, pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel);
  else if (f[0] == 0xD0) parseNan(f, len, pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel);
}

// ---------- Bluetooth ----------

void onBleMessage(const uint8_t *msg, size_t len, const char *addr, int rssi) {
  withDrone(nullptr, addr, Transport::Ble, 0, rssi, [&](Drone &d) {
    if ((msg[0] >> 4) == 0xF) decodePack(d, msg, len);
    else if (len >= ODID_MSG_SIZE) decodeMessage(d, msg);
  });
}

// ---------- watch log (persisted in NVS) ----------

const size_t LOG_MAX = 40;
const uint32_t LOG_SAVE_MIN_MS = 5UL * 60 * 1000;  // throttle flash writes for updates

std::vector<LogEntry> logv;
bool logDirty = false;
uint32_t logLastSave = 0;
uint32_t logNew = 0;
String logNewTitle;

// Watch alerts (guarded by mtx)
std::vector<String> alertQueue;
struct Snooze {
  String id;
  uint32_t until;
};
std::vector<Snooze> snoozes;
struct Ack {
  String id;
  uint32_t lastHeard;
};
std::vector<Ack> acks;

bool snoozedLocked(const String &id, uint32_t now) {
  snoozes.erase(std::remove_if(snoozes.begin(), snoozes.end(),
                               [&](const Snooze &z) { return (int32_t)(now - z.until) >= 0; }),
                snoozes.end());
  for (const auto &z : snoozes)
    if (z.id == id) return true;
  return false;
}

// True if alerts for `id` are acknowledged; refreshes the "still around" time
bool ackedLocked(const String &id, uint32_t now, bool heardNow) {
  acks.erase(std::remove_if(acks.begin(), acks.end(),
                            [&](const Ack &a) { return now - a.lastHeard > ACK_GONE_MS; }),
             acks.end());
  for (auto &a : acks) {
    if (a.id == id) {
      if (heardNow) a.lastHeard = now;
      return true;
    }
  }
  return false;
}

void forgetAlertStateLocked(const String &id) {
  snoozes.erase(std::remove_if(snoozes.begin(), snoozes.end(), [&](const Snooze &z) { return z.id == id; }),
                snoozes.end());
  acks.erase(std::remove_if(acks.begin(), acks.end(), [&](const Ack &a) { return a.id == id; }), acks.end());
  alertQueue.erase(std::remove(alertQueue.begin(), alertQueue.end(), id), alertQueue.end());
}

void queueAlertLocked(const String &id) {
  uint32_t now = millis();
  if (snoozedLocked(id, now)) return;
  if (ackedLocked(id, now, true)) return;
  if (std::find(alertQueue.begin(), alertQueue.end(), id) != alertQueue.end()) return;
  alertQueue.push_back(id);
}
Preferences logPrefs;

uint32_t epochNow() {
  time_t t = time(nullptr);
  return t > 1700000000 ? (uint32_t)t : 0;  // 0 = clock not set yet
}

void logLoad() {
  logPrefs.begin("drone", false);
  size_t n = logPrefs.isKey("log") ? logPrefs.getBytesLength("log") : 0;
  if (n && n % sizeof(LogEntry) == 0 && n / sizeof(LogEntry) <= LOG_MAX) {
    logv.resize(n / sizeof(LogEntry));
    logPrefs.getBytes("log", logv.data(), n);
  }
  logPrefs.end();
  Serial.printf("[rid] watch log: %u drones\n", (unsigned)logv.size());
}

void logSave() {  // caller holds mtx
  logPrefs.begin("drone", false);
  if (logv.empty()) logPrefs.remove("log");
  else logPrefs.putBytes("log", logv.data(), logv.size() * sizeof(LogEntry));
  logPrefs.end();
  logDirty = false;
  logLastSave = millis();
}

void copyStr(char *dst, size_t n, const String &s) {
  strncpy(dst, s.c_str(), n - 1);
  dst[n - 1] = 0;
}

// Fold drones heard since `since` into the log (holds mtx)
void logMerge(uint32_t since) {
  bool added = false;
  uint32_t now = epochNow();
  for (const auto &d : drones) {
    if ((int32_t)(d.lastSeen - since) < 0) continue;
    // Match by Remote ID first (BLE addresses can rotate), then by MAC
    auto it = std::find_if(logv.begin(), logv.end(), [&](const LogEntry &e) {
      return d.uasId.length() ? d.uasId == e.id : d.key == e.key;
    });
    if (it == logv.end()) {
      if (logv.size() >= LOG_MAX) {  // drop the entry heard longest ago
        logv.erase(std::min_element(logv.begin(), logv.end(), [](const LogEntry &a, const LogEntry &b) {
          return a.last < b.last;
        }));
      }
      LogEntry e = {};
      e.first = now;
      e.rssiMax = -127;
      logv.push_back(e);
      it = logv.end() - 1;
      added = true;
      logNew++;
      logNewTitle = d.uasId.length() ? d.uasId : d.key;
      Serial.printf("[rid] watch: new drone %s\n", logNewTitle.c_str());
    }
    queueAlertLocked(d.uasId.length() ? d.uasId : d.key);
    copyStr(it->key, sizeof(it->key), d.key);
    if (d.uasId.length()) copyStr(it->id, sizeof(it->id), d.uasId);
    it->idType = d.idType;
    it->uaType = d.uaType;
    it->transport = (uint8_t)d.transport;
    it->last = now;
    it->sightings++;
    it->rssiLast = d.rssi;
    if (d.rssi > it->rssiMax) it->rssiMax = d.rssi;
    if (isDji(d)) it->flags |= LOG_DJI;
    if (d.status == 3) it->flags |= LOG_EMERGENCY;
    if (d.hasLoc) {
      it->flags |= LOG_HAS_LOC;
      it->lat = (int32_t)lround(d.lat * 1e7);
      it->lon = (int32_t)lround(d.lon * 1e7);
      it->alt = isnan(d.altGeo) ? INT16_MIN : (int16_t)lroundf(d.altGeo);
    }
    if (d.hasOp) {
      it->flags |= LOG_HAS_OP;
      it->opLat = (int32_t)lround(d.opLat * 1e7);
      it->opLon = (int32_t)lround(d.opLon * 1e7);
    }
    logDirty = true;
  }
  if (added || (logDirty && millis() - logLastSave > LOG_SAVE_MIN_MS)) logSave();
}

// ---------- radio on/off and modes ----------

Mode md = Mode::Off;
bool burst = false;             // watch: currently in the scanning part of the cycle
uint32_t phaseEnd = 0;
uint32_t burstStart = 0;

void updateLed() {
  uint32_t now = millis();
  bool recent = false;
  {
    std::lock_guard<std::mutex> lock(mtx);
    for (const auto &d : drones)
      if (now - d.lastSeen < STALE_MS) recent = true;
  }
  // 1 = scanning, 2 = drone nearby, 3 = watch resting
  int want = !active ? 3 : (recent ? 2 : 1);
  if (want == ledState) return;
  ledState = want;
  if (want == 3) led::set(led::Color::Cyan);
  else if (recent) led::blink(led::Color::Blue, 250);
  else led::set(led::Color::Blue);
}

void radioOn() {
  if (active) return;
  net::suspend();  // promiscuous channel hopping needs the station disconnected
  {
    std::lock_guard<std::mutex> lock(mtx);
    active = true;
  }
  wifi_promiscuous_filter_t filt = {};
  filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filt);
  esp_wifi_set_promiscuous_rx_cb(snifferCb);
  esp_wifi_set_promiscuous(true);
  hopIdx = 0;
  nextHop = 0;
  channel = 0;  // force the first hop to set the channel
  if (!ble::startRid(onBleMessage)) Serial.println("[rid] Bluetooth unavailable, Wi-Fi only");
}

void radioOff() {
  if (!active) return;
  {
    std::lock_guard<std::mutex> lock(mtx);
    active = false;
  }
  esp_wifi_set_promiscuous(false);
  ble::stop();
  net::resume();
}

void resetSession() {
  std::lock_guard<std::mutex> lock(mtx);
  drones.clear();
  frameCount = 0;
  huntKey = "";
  huntChannel = 0;
}

}  // namespace

void begin() { logLoad(); }

bool startScan() {
  stop();
  resetSession();
  md = Mode::Scan;
  ledState = -1;
  radioOn();
  Serial.println("[rid] continuous scan started");
  return true;
}

bool startWatch() {
  stop();
  resetSession();
  {
    std::lock_guard<std::mutex> lock(mtx);
    acks.clear();  // a new watch session alerts fresh
  }
  md = Mode::Watch;
  ledState = -1;
  burst = true;
  burstStart = millis();
  phaseEnd = burstStart + WATCH_SCAN_MS;
  radioOn();
  Serial.println("[rid] drone watch started");
  return true;
}

void stop() {
  if (md == Mode::Off) return;
  if (md == Mode::Watch && active) {  // keep what the interrupted burst heard
    std::lock_guard<std::mutex> lock(mtx);
    logMerge(burstStart);
  }
  radioOff();
  {
    std::lock_guard<std::mutex> lock(mtx);
    huntKey = "";
    huntChannel = 0;
    alertQueue.clear();
    if (logDirty) logSave();
  }
  md = Mode::Off;
  burst = false;
  led::set(led::Color::Green);
  Serial.println("[rid] stopped, Wi-Fi resuming");
}

Mode mode() { return md; }
bool running() { return md != Mode::Off; }
bool radioActive() { return active; }

uint32_t phaseRemainingMs() {
  if (md != Mode::Watch) return 0;
  int32_t left = (int32_t)(phaseEnd - millis());
  return left > 0 ? left : 0;
}

void loop() {
  if (md == Mode::Off) return;
  uint32_t now = millis();

  if (md == Mode::Watch && (int32_t)(now - phaseEnd) >= 0) {
    if (burst) {  // scan finished: log what we heard, rest with Wi-Fi back on
      {
        std::lock_guard<std::mutex> lock(mtx);
        logMerge(burstStart);
      }
      radioOff();
      burst = false;
      phaseEnd = now + WATCH_REST_MS;
    } else {      // rest finished: next scan
      burst = true;
      burstStart = now;
      phaseEnd = now + WATCH_SCAN_MS;
      radioOn();
    }
  }

  if (active && (int32_t)(now - nextHop) >= 0) {
    uint8_t want = huntChannel ? huntChannel : HOP_SEQ[hopIdx];
    if (!huntChannel) hopIdx = (hopIdx + 1) % HOP_LEN;
    if (want != channel) {
      channel = want;
      esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    }
    nextHop = now + HOP_DWELL_MS;
  }
  {
    std::lock_guard<std::mutex> lock(mtx);
    if (huntKey.length()) huntAcc.tick(huntInfo, now);
  }
  static uint32_t lastLed = 0;
  if (now - lastLed > 500) {
    lastLed = now;
    updateLed();
  }
}

// ---------- watch log API ----------

std::vector<LogEntry> watchLog() {
  std::vector<LogEntry> out;
  {
    std::lock_guard<std::mutex> lock(mtx);
    out = logv;
  }
  std::sort(out.begin(), out.end(), [](const LogEntry &a, const LogEntry &b) { return a.last > b.last; });
  return out;
}

bool logFind(const String &key, LogEntry &out) {
  std::lock_guard<std::mutex> lock(mtx);
  for (const auto &e : logv) {
    if (key == e.key || (e.id[0] && key == e.id)) {
      out = e;
      return true;
    }
  }
  return false;
}

void logDelete(const String &key) {
  std::lock_guard<std::mutex> lock(mtx);
  for (const auto &e : logv) {  // forget snooze/ack for this drone (by ID and MAC)
    if (key == e.key || (e.id[0] && key == e.id)) {
      if (e.id[0]) forgetAlertStateLocked(e.id);
      forgetAlertStateLocked(e.key);
    }
  }
  forgetAlertStateLocked(key);
  logv.erase(std::remove_if(logv.begin(), logv.end(),
                            [&](const LogEntry &e) { return key == e.key || (e.id[0] && key == e.id); }),
             logv.end());
  logSave();
}

void logClear() {
  std::lock_guard<std::mutex> lock(mtx);
  logv.clear();
  snoozes.clear();
  acks.clear();
  alertQueue.clear();
  logSave();
}

size_t logCount() {
  std::lock_guard<std::mutex> lock(mtx);
  return logv.size();
}

uint32_t logNewCount() { return logNew; }

bool nextAlert(String &id) {
  std::lock_guard<std::mutex> lock(mtx);
  while (!alertQueue.empty()) {
    id = alertQueue.front();
    alertQueue.erase(alertQueue.begin());
    uint32_t now = millis();  // may have been snoozed or acknowledged while queued
    if (!snoozedLocked(id, now) && !ackedLocked(id, now, false)) return true;
  }
  return false;
}

void snooze(const String &id, uint32_t ms) {
  std::lock_guard<std::mutex> lock(mtx);
  uint32_t now = millis();
  snoozedLocked(id, now);  // prunes expired entries
  for (auto &z : snoozes) {
    if (z.id == id) {
      z.until = now + ms;
      return;
    }
  }
  snoozes.push_back({id, now + ms});
  Serial.printf("[rid] alerts for %s snoozed for %lu min\n", id.c_str(), (unsigned long)(ms / 60000));
}

void acknowledge(const String &id) {
  std::lock_guard<std::mutex> lock(mtx);
  uint32_t now = millis();
  if (ackedLocked(id, now, true)) return;
  acks.push_back({id, now});
}

bool watchDroneNearby() {
  if (md != Mode::Watch) return false;
  uint32_t now = millis();
  const uint32_t window = WATCH_SCAN_MS + WATCH_REST_MS + 5000;  // one full cycle + margin
  std::lock_guard<std::mutex> lock(mtx);
  for (const auto &d : drones)
    if (now - d.lastSeen < window) return true;
  return false;
}

String logNewestTitle() {
  std::lock_guard<std::mutex> lock(mtx);
  return logNewTitle;
}

std::vector<Drone> list() {
  std::vector<Drone> out;
  {
    std::lock_guard<std::mutex> lock(mtx);
    out = drones;
  }
  std::sort(out.begin(), out.end(), [](const Drone &a, const Drone &b) { return a.lastSeen > b.lastSeen; });
  return out;
}

bool find(const String &key, Drone &out) {
  std::lock_guard<std::mutex> lock(mtx);
  for (const auto &d : drones) {
    if (d.key == key) {
      out = d;
      return true;
    }
  }
  return false;
}

uint8_t currentChannel() { return channel; }
uint32_t frames() { return frameCount; }

void setHuntTarget(const String &key) {
  std::lock_guard<std::mutex> lock(mtx);
  huntKey = key;
  huntChannel = 0;
  for (const auto &d : drones)
    if (d.key == key && d.transport != Transport::Ble && d.channel) huntChannel = d.channel;
  huntInfo = HuntInfo();
  huntAcc.reset(millis());
}

HuntInfo hunt() {
  std::lock_guard<std::mutex> lock(mtx);
  return huntInfo;
}

bool isDji(const Drone &d) {
  // ANSI/CTA-2063 serial numbers start with the manufacturer code; DJI's is 1581F
  return d.idType == 1 && d.uasId.startsWith("1581F");
}

const char *uaTypeName(uint8_t t) {
  static const char *names[] = {"Not declared",  "Aeroplane",      "Multirotor",     "Gyroplane",
                                "VTOL",          "Ornithopter",    "Glider",         "Kite",
                                "Free balloon",  "Captive balloon", "Airship",       "Parachute",
                                "Rocket",        "Tethered",       "Ground obstacle", "Other"};
  return t < 16 ? names[t] : "Unknown";
}

const char *statusName(uint8_t s) {
  switch (s) {
    case 1: return "On ground";
    case 2: return "Airborne";
    case 3: return "EMERGENCY";
    case 4: return "RID system failure";
    default: return "Undeclared";
  }
}

const char *idTypeName(uint8_t t) {
  switch (t) {
    case 1: return "Serial number";
    case 2: return "CAA registration";
    case 3: return "UTM UUID";
    case 4: return "Session ID";
    default: return "None";
  }
}

const char *transportName(Transport t) {
  switch (t) {
    case Transport::Ble: return "Bluetooth";
    case Transport::WifiNan: return "Wi-Fi NaN";
    default: return "Wi-Fi beacon";
  }
}

float distanceM(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0, k = M_PI / 180.0;
  double dLat = (lat2 - lat1) * k, dLon = (lon2 - lon1) * k;
  double a = sin(dLat / 2) * sin(dLat / 2) + cos(lat1 * k) * cos(lat2 * k) * sin(dLon / 2) * sin(dLon / 2);
  return (float)(2 * R * atan2(sqrt(a), sqrt(1 - a)));
}

}  // namespace drone
