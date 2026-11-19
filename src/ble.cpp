#include "ble.h"

#include <NimBLEDevice.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp32-hal-bt.h>

#include <algorithm>
#include <mutex>

namespace ble {
namespace {

const size_t MAX_DEVICES = 80;
const size_t MAX_IGNORED = 60;
const size_t MAX_TRACKERS = 30;
const uint32_t TRACKER_FORGET_MS = 30UL * 60 * 1000;

std::mutex mtx;                      // callbacks run on the NimBLE host task
std::vector<Device> devices;
std::vector<IgnoredDevice> ignored;
std::vector<TrackerSighting> trackerList;
bool ignoredLoaded = false;

NimBLEScan *scan = nullptr;
bool inited = false;
uint32_t lastBusyMs = 0;  // last time any Bluetooth tool was using the stack
// NimBLE keeps ~20 KB of heap while initialised. HTTPS downloads (NWS alerts,
// NDBC buoys) need ~50 KB, so the stack is shut down once no tool has used it
// for this long. It starts again by itself on next use. Longer than Drone
// Watch's 25 s rest, so a running watch keeps it.
const uint32_t RELEASE_IDLE_MS = 60000;
bool scannedOnce = false;
Mode md = Mode::Idle;
uint32_t scanEnd = 0;
uint32_t scanRound = 0;

String huntAddr;
HuntInfo huntInfo;
RidSink ridSink = nullptr;
HuntAccumulator huntAcc;

Preferences prefs;

// --- ignore list persistence: "addr\tname\n" per entry ---
void loadIgnored() {
  if (ignoredLoaded) return;
  ignoredLoaded = true;
  prefs.begin("ble", false);  // read-write: creates the namespace on first use
  String s = prefs.isKey("ign") ? prefs.getString("ign") : String();
  prefs.end();
  int start = 0;
  while (start < (int)s.length()) {
    int nl = s.indexOf('\n', start);
    if (nl < 0) nl = s.length();
    String line = s.substring(start, nl);
    int tab = line.indexOf('\t');
    if (tab > 0) ignored.push_back({line.substring(0, tab), line.substring(tab + 1)});
    start = nl + 1;
  }
}

void saveIgnored() {  // caller holds mtx
  String s;
  for (const auto &d : ignored) s += d.addr + "\t" + d.name + "\n";
  prefs.begin("ble", false);
  prefs.putString("ign", s);
  prefs.end();
}

bool isIgnoredLocked(const String &addr) {
  for (const auto &d : ignored)
    if (d.addr == addr) return true;
  return false;
}

bool hasUuid(const NimBLEAdvertisedDevice *d, uint16_t uuid16) {
  NimBLEUUID u(uuid16);
  if (d->isAdvertisingService(u)) return true;
  for (uint8_t i = 0; i < d->getServiceDataCount(); i++)
    if (d->getServiceDataUUID(i) == u) return true;
  return false;
}

Tracker classify(const NimBLEAdvertisedDevice *d) {
  if (d->haveManufacturerData()) {
    std::string m = d->getManufacturerData();
    // Apple (0x004C) "offline finding" payload type 0x12: AirTags and other
    // Find My accessories broadcast this when away from their owner
    if (m.size() >= 3 && (uint8_t)m[0] == 0x4C && (uint8_t)m[1] == 0x00 && (uint8_t)m[2] == 0x12)
      return Tracker::AppleFindMy;
  }
  if (hasUuid(d, 0xFEED) || hasUuid(d, 0xFEEC)) return Tracker::Tile;
  if (hasUuid(d, 0xFD5A)) return Tracker::SmartTag;
  if (hasUuid(d, 0xFCB2)) return Tracker::Dult;  // cross-platform unwanted-tracker standard
  // Google Find My Device network: Eddystone service data, frame type 0x40/0x41
  NimBLEUUID eddystone((uint16_t)0xFEAA);
  for (uint8_t i = 0; i < d->getServiceDataCount(); i++) {
    if (d->getServiceDataUUID(i) == eddystone) {
      std::string sd = d->getServiceData(i);
      if (!sd.empty() && ((uint8_t)sd[0] == 0x40 || (uint8_t)sd[0] == 0x41))
        return Tracker::GoogleFindMy;
    }
  }
  return Tracker::None;
}

void updateDevice(Device &dv, const NimBLEAdvertisedDevice *d, uint32_t now) {
  dv.rssi = d->getRSSI();
  dv.lastSeen = now;
  dv.seenCount++;
  dv.randomAddr = d->getAddressType() != BLE_ADDR_PUBLIC;
  if (d->haveName() && !d->getName().empty()) dv.name = d->getName().c_str();
  if (d->haveManufacturerData()) {
    std::string m = d->getManufacturerData();
    if (m.size() >= 2) {
      dv.hasMfg = true;
      dv.mfgId = (uint8_t)m[0] | ((uint8_t)m[1] << 8);  // company ID, little-endian
    }
  }
  if (d->haveTXPower()) {
    dv.hasTx = true;
    dv.tx = d->getTXPower();
  }
  if (d->haveServiceUUID()) dv.service = d->getServiceUUID().toString().c_str();
  Tracker t = classify(d);
  if (t != Tracker::None) dv.tracker = t;
}

void recordTracker(const String &addr, Tracker t, int rssi, uint32_t now) {  // holds mtx
  auto it = std::find_if(trackerList.begin(), trackerList.end(),
                         [&](const TrackerSighting &x) { return x.addr == addr; });
  if (it == trackerList.end()) {
    if (trackerList.size() >= MAX_TRACKERS) return;
    TrackerSighting s;
    s.addr = addr;
    s.type = t;
    s.firstSeen = now;
    trackerList.push_back(s);
    it = trackerList.end() - 1;
  }
  it->rssi = rssi;
  it->lastSeen = now;
  if (it->lastRound != scanRound) {
    it->lastRound = scanRound;
    it->rounds++;
  }
}

class Callbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *d) override {
    if (md == Mode::Rid) {  // drone scanner: only Remote ID service data matters
      RidSink sink = ridSink;
      if (!sink) return;
      NimBLEUUID rid((uint16_t)0xFFFA);  // ASTM Remote ID
      for (uint8_t i = 0; i < d->getServiceDataCount(); i++) {
        if (!(d->getServiceDataUUID(i) == rid)) continue;
        std::string sd = d->getServiceData(i);
        // [0x0D app code][message counter][25-byte message]
        if (sd.size() >= 27 && (uint8_t)sd[0] == 0x0D) {
          std::string a = d->getAddress().toString();
          sink((const uint8_t *)sd.data() + 2, sd.size() - 2, a.c_str(), d->getRSSI());
        }
      }
      return;
    }

    String addr = d->getAddress().toString().c_str();
    uint32_t now = millis();
    std::lock_guard<std::mutex> lock(mtx);

    if (md == Mode::Hunt) {
      if (addr == huntAddr) huntAcc.add(d->getRSSI(), now);
      return;
    }
    if (md != Mode::Scan || isIgnoredLocked(addr)) return;

    auto it = std::find_if(devices.begin(), devices.end(),
                           [&](const Device &x) { return x.addr == addr; });
    if (it == devices.end()) {
      if (devices.size() >= MAX_DEVICES) return;
      Device nd;
      nd.addr = addr;
      nd.firstSeen = now;
      devices.push_back(nd);
      it = devices.end() - 1;
    }
    updateDevice(*it, d, now);
    if (it->tracker != Tracker::None) recordTracker(addr, it->tracker, it->rssi, now);
  }
};
Callbacks callbacks;

bool ensureStarted() {
  lastBusyMs = millis();
  if (inited) return true;
  Serial.printf("[ble] starting NimBLE, free heap %lu\n", (unsigned long)ESP.getFreeHeap());
  // The ESP32 refuses to run Bluetooth alongside Wi-Fi unless Wi-Fi modem
  // sleep is on, so re-enable it (it stays on until reboot).
  WiFi.setSleep(true);
  // Reference esp32-hal-bt so the linker keeps Arduino's btInUse() override;
  // otherwise Arduino frees the controller's memory at boot.
  (void)btStarted();

  NimBLEDevice::init("");
  scan = NimBLEDevice::getScan();
  if (!scan) {
    Serial.println("[ble] init failed");
    return false;
  }
  // NimBLE delivers every advert without leaking (unlike the Bluedroid
  // library), and we keep our own device list, so store no results.
  scan->setScanCallbacks(&callbacks, true /*want duplicates: live RSSI*/);
  scan->setMaxResults(0);
  scan->setActiveScan(true);  // request scan responses, which often carry the name
  scan->setInterval(100);
  scan->setWindow(99);
  inited = true;
  Serial.printf("[ble] ready, free heap %lu\n", (unsigned long)ESP.getFreeHeap());
  return true;
}

void radioStop() {
  if (scan && scan->isScanning()) scan->stop();
}

}  // namespace

bool startScan() {
  if (!ensureStarted()) return false;
  radioStop();
  loadIgnored();
  {
    std::lock_guard<std::mutex> lock(mtx);
    devices.clear();
    scanRound++;
    md = Mode::Scan;
  }
  scannedOnce = true;
  scan->start(SCAN_SECS * 1000, false, true);  // non-blocking, milliseconds
  scanEnd = millis() + SCAN_SECS * 1000 + 200;
  return true;
}

bool startHunt(const String &addr) {
  if (!ensureStarted()) return false;
  radioStop();
  {
    std::lock_guard<std::mutex> lock(mtx);
    huntAddr = addr;
    huntInfo = HuntInfo();
    huntAcc.reset(millis());
    md = Mode::Hunt;
  }
  scan->start(0, false, true);  // 0 = until stopped
  return true;
}

bool startRid(RidSink sink) {
  if (!ensureStarted()) return false;
  radioStop();
  {
    std::lock_guard<std::mutex> lock(mtx);
    ridSink = sink;
    md = Mode::Rid;
  }
  scan->start(0, false, true);  // until stopped
  return true;
}

void pauseRid(bool paused) {
  if (md != Mode::Rid || !scan) return;
  if (paused) {
    if (scan->isScanning()) scan->stop();
  } else if (!scan->isScanning()) {
    scan->start(0, false, true);
  }
}

void stop() {
  radioStop();
  std::lock_guard<std::mutex> lock(mtx);
  md = Mode::Idle;
}

Mode mode() { return md; }
bool stackActive() { return inited; }
bool everScanned() { return scannedOnce; }

uint32_t scanRemainingMs() {
  if (md != Mode::Scan) return 0;
  int32_t left = (int32_t)(scanEnd - millis());
  return left > 0 ? left : 0;
}

void loop() {
  uint32_t now = millis();
  if (md != Mode::Idle) lastBusyMs = now;
  if (inited && md == Mode::Idle && now - lastBusyMs > RELEASE_IDLE_MS) {
    radioStop();
    NimBLEDevice::deinit(true);  // frees the host and controller memory
    scan = nullptr;
    inited = false;
    Serial.printf("[ble] idle, Bluetooth released, free heap %lu, largest block %lu\n",
                  (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap());
  }
  if (md == Mode::Scan && (int32_t)(now - scanEnd) >= 0) {
    radioStop();
    std::lock_guard<std::mutex> lock(mtx);
    md = Mode::Idle;
    Serial.printf("[ble] scan done, %u devices, %u trackers\n", (unsigned)devices.size(),
                  (unsigned)trackerList.size());
  } else if (md == Mode::Hunt) {
    std::lock_guard<std::mutex> lock(mtx);
    huntAcc.tick(huntInfo, now);
  }

  static uint32_t lastPrune = 0;
  if (now - lastPrune > 10000) {
    lastPrune = now;
    std::lock_guard<std::mutex> lock(mtx);
    trackerList.erase(std::remove_if(trackerList.begin(), trackerList.end(),
                                     [&](const TrackerSighting &t) {
                                       return now - t.lastSeen > TRACKER_FORGET_MS;
                                     }),
                      trackerList.end());
  }
}

size_t count() {
  std::lock_guard<std::mutex> lock(mtx);
  return devices.size();
}

std::vector<Device> snapshot(size_t maxCount, size_t *total) {
  // Sort indices under the lock and copy only the top entries: copying the
  // whole list (Strings and all) every refresh costs heap.
  std::vector<Device> out;
  std::lock_guard<std::mutex> lock(mtx);
  if (total) *total = devices.size();
  std::vector<uint8_t> idx(devices.size());
  for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
  std::sort(idx.begin(), idx.end(),
            [](uint8_t a, uint8_t b) { return devices[a].rssi > devices[b].rssi; });
  size_t n = idx.size() < maxCount ? idx.size() : maxCount;
  out.reserve(n);
  for (size_t i = 0; i < n; i++) out.push_back(devices[idx[i]]);
  return out;
}

bool find(const String &addr, Device &out) {
  std::lock_guard<std::mutex> lock(mtx);
  for (const auto &d : devices) {
    if (d.addr == addr) {
      out = d;
      return true;
    }
  }
  return false;
}

HuntInfo hunt() {
  std::lock_guard<std::mutex> lock(mtx);
  return huntInfo;
}

std::vector<TrackerSighting> trackers() {
  std::vector<TrackerSighting> out;
  {
    std::lock_guard<std::mutex> lock(mtx);
    out = trackerList;
  }
  std::sort(out.begin(), out.end(), [](const TrackerSighting &a, const TrackerSighting &b) {
    return (a.lastSeen - a.firstSeen) > (b.lastSeen - b.firstSeen);
  });
  return out;
}

bool isFollowing(const TrackerSighting &t) {
  return t.lastSeen - t.firstSeen >= FOLLOW_MIN_MS && t.rounds >= FOLLOW_MIN_ROUNDS;
}

void markAlerted(const String &addr) {
  std::lock_guard<std::mutex> lock(mtx);
  for (auto &t : trackerList)
    if (t.addr == addr) t.alerted = true;
}

void clearTrackers() {
  std::lock_guard<std::mutex> lock(mtx);
  trackerList.clear();
}

const char *trackerName(Tracker t) {
  switch (t) {
    case Tracker::AppleFindMy: return "Apple Find My";
    case Tracker::Tile: return "Tile";
    case Tracker::SmartTag: return "Samsung SmartTag";
    case Tracker::GoogleFindMy: return "Google Find My";
    case Tracker::Dult: return "Tracker (DULT)";
    default: return "";
  }
}

void ignore(const String &addr, const String &name) {
  loadIgnored();
  String clean = name;
  clean.replace("\t", " ");
  clean.replace("\n", " ");
  std::lock_guard<std::mutex> lock(mtx);
  if (!isIgnoredLocked(addr)) {
    if (ignored.size() >= MAX_IGNORED) ignored.erase(ignored.begin());  // drop the oldest
    ignored.push_back({addr, clean});
    saveIgnored();
  }
  devices.erase(std::remove_if(devices.begin(), devices.end(),
                               [&](const Device &d) { return d.addr == addr; }),
                devices.end());
  trackerList.erase(std::remove_if(trackerList.begin(), trackerList.end(),
                                   [&](const TrackerSighting &t) { return t.addr == addr; }),
                    trackerList.end());
}

void unignore(const String &addr) {
  loadIgnored();
  std::lock_guard<std::mutex> lock(mtx);
  ignored.erase(std::remove_if(ignored.begin(), ignored.end(),
                               [&](const IgnoredDevice &d) { return d.addr == addr; }),
                ignored.end());
  saveIgnored();
}

void clearIgnored() {
  loadIgnored();
  std::lock_guard<std::mutex> lock(mtx);
  ignored.clear();
  saveIgnored();
}

size_t ignoredCount() {
  loadIgnored();
  std::lock_guard<std::mutex> lock(mtx);
  return ignored.size();
}

std::vector<IgnoredDevice> ignoredList() {
  loadIgnored();
  std::lock_guard<std::mutex> lock(mtx);
  return ignored;
}

const char *vendorName(uint16_t id) {
  switch (id) {  // Bluetooth SIG company identifiers
    case 0x0006: return "Microsoft";
    case 0x000D: return "Texas Instruments";
    case 0x004C: return "Apple";
    case 0x0059: return "Nordic Semi";
    case 0x0075: return "Samsung";
    case 0x0087: return "Garmin";
    case 0x009E: return "Bose";
    case 0x00E0: return "Google";
    case 0x012D: return "Sony";
    case 0x0171: return "Amazon";
    case 0x02E5: return "Espressif";
    case 0x038F: return "Xiaomi";
    case 0x0499: return "Ruuvi";
    default: return nullptr;
  }
}

}  // namespace ble
