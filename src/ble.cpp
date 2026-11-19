#include "ble.h"

#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_bt.h>

#include <algorithm>
#include <mutex>

namespace ble {
namespace {

const uint32_t HUNT_CYCLE_SECS = 1;  // one RSSI report per cycle (duplicates are off)
const size_t MAX_DEVICES = 80;
const size_t MAX_IGNORED = 60;
const float HUNT_ALPHA = 0.4f;       // smoothing: higher reacts faster, noisier

std::mutex mtx;                      // callbacks run on the Bluetooth task
std::vector<Device> devices;
std::vector<IgnoredDevice> ignored;
bool ignoredLoaded = false;

BLEScan *scan = nullptr;
bool inited = false;
bool scannedOnce = false;
Mode md = Mode::Idle;
uint32_t scanEnd = 0;
uint32_t cycleEnd = 0;

String huntAddr;
HuntInfo huntInfo;

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

void updateDevice(Device &dv, BLEAdvertisedDevice &d, uint32_t now) {
  dv.rssi = d.getRSSI();
  dv.lastSeen = now;
  dv.seenCount++;
  dv.randomAddr = d.getAddressType() != BLE_ADDR_TYPE_PUBLIC;
  if (d.haveName() && d.getName().length()) dv.name = d.getName().c_str();
  if (d.haveManufacturerData()) {
    auto m = d.getManufacturerData();
    if (m.length() >= 2) {
      const uint8_t *p = (const uint8_t *)m.c_str();
      dv.hasMfg = true;
      dv.mfgId = p[0] | (p[1] << 8);  // company ID, little-endian
    }
  }
  if (d.haveTXPower()) {
    dv.hasTx = true;
    dv.tx = d.getTXPower();
  }
  if (d.haveServiceUUID()) dv.service = d.getServiceUUID().toString().c_str();
}

class Callbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice d) override {
    String addr = d.getAddress().toString().c_str();
    uint32_t now = millis();
    std::lock_guard<std::mutex> lock(mtx);

    if (md == Mode::Hunt) {
      if (addr != huntAddr) return;
      int r = d.getRSSI();
      huntInfo.smooth = huntInfo.samples ? huntInfo.smooth + HUNT_ALPHA * (r - huntInfo.smooth)
                                         : (float)r;
      huntInfo.rssi = r;
      huntInfo.lastSeen = now;
      huntInfo.samples++;
      for (auto &dv : devices)
        if (dv.addr == addr) updateDevice(dv, d, now);
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
  }
};
Callbacks callbacks;

bool ensureStarted() {
  if (inited) return true;
  Serial.printf("[ble] starting Bluetooth, free heap %lu\n", (unsigned long)ESP.getFreeHeap());
  // The ESP32 refuses to run Bluetooth alongside Wi-Fi unless Wi-Fi modem
  // sleep is on, so re-enable it (it stays on until reboot).
  WiFi.setSleep(true);

  // We only need BLE: hand Classic Bluetooth's reserved RAM back to the heap
  // and bring the controller up in BLE-only mode. BLEDevice::init() sees the
  // controller already running and skips its own (dual-mode) start.
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    cfg.mode = ESP_BT_MODE_BLE;
    if (esp_bt_controller_init(&cfg) != ESP_OK ||
        esp_bt_controller_enable(ESP_BT_MODE_BLE) != ESP_OK) {
      Serial.println("[ble] BLE-only controller start failed, using default");
    }
  }
  Serial.printf("[ble] controller up, free heap %lu\n", (unsigned long)ESP.getFreeHeap());

  BLEDevice::init("");
  scan = BLEDevice::getScan();
  if (!scan) {
    Serial.println("[ble] init failed");
    return false;
  }
  // wantDuplicates must stay false: with it on, the Arduino BLE library
  // allocates a new BLEAdvertisedDevice for every repeat advert and never
  // frees it (std::map::insert of an existing key), exhausting the heap.
  scan->setAdvertisedDeviceCallbacks(&callbacks, false);
  scan->setActiveScan(true);  // request scan responses, which often carry the name
  scan->setInterval(100);
  scan->setWindow(99);
  inited = true;
  Serial.printf("[ble] ready, free heap %lu\n", (unsigned long)ESP.getFreeHeap());
  return true;
}

void radioStop() {
  if (scan && md != Mode::Idle) scan->stop();
  if (scan) scan->clearResults();
}

}  // namespace

bool startScan() {
  if (!ensureStarted()) return false;
  radioStop();
  loadIgnored();
  {
    std::lock_guard<std::mutex> lock(mtx);
    devices.clear();
    md = Mode::Scan;
  }
  scannedOnce = true;
  scan->start(SCAN_SECS, nullptr, false);  // non-blocking
  scanEnd = millis() + SCAN_SECS * 1000 + 200;
  return true;
}

bool startHunt(const String &addr) {
  if (!ensureStarted()) return false;
  radioStop();
  std::lock_guard<std::mutex> lock(mtx);
  huntAddr = addr;
  huntInfo = HuntInfo();
  md = Mode::Hunt;
  cycleEnd = 0;  // loop() starts the first cycle
  return true;
}

void stop() {
  radioStop();
  std::lock_guard<std::mutex> lock(mtx);
  md = Mode::Idle;
}

Mode mode() { return md; }
bool everScanned() { return scannedOnce; }

uint32_t scanRemainingMs() {
  if (md != Mode::Scan) return 0;
  int32_t left = (int32_t)(scanEnd - millis());
  return left > 0 ? left : 0;
}

void loop() {
  uint32_t now = millis();
  if (md == Mode::Scan && (int32_t)(now - scanEnd) >= 0) {
    scan->clearResults();  // library's copy; ours stays
    std::lock_guard<std::mutex> lock(mtx);
    md = Mode::Idle;
    Serial.printf("[ble] scan done, %u devices\n", (unsigned)devices.size());
  } else if (md == Mode::Hunt && (int32_t)(now - cycleEnd) >= 0) {
    scan->clearResults();  // forget the target so the next cycle reports it again
    bool ok = scan->start(HUNT_CYCLE_SECS, nullptr, false);
    cycleEnd = now + (ok ? HUNT_CYCLE_SECS * 1000 + 150 : 100);
  }
}

size_t count() {
  std::lock_guard<std::mutex> lock(mtx);
  return devices.size();
}

std::vector<Device> snapshot(size_t maxCount, size_t *total) {
  // Sort indices under the lock and copy only the top entries: copying the
  // whole list (Strings and all) every refresh costs heap we don't have.
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
