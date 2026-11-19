#include "ble.h"

#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <WiFi.h>
#include <esp_bt.h>

#include <algorithm>
#include <mutex>

namespace ble {
namespace {

// Each cycle reports every device once (duplicates off, see start()), so a
// short cycle doubles as the RSSI refresh rate. Restarting also frees the
// library's own result cache.
const uint32_t CYCLE_SECS = 3;
const uint32_t FORGET_MS = 60000;   // drop devices not heard from for 60 s
const size_t MAX_DEVICES = 80;

std::mutex mtx;                     // onResult runs on the Bluetooth task
std::vector<Device> devices;
BLEScan *scan = nullptr;
bool inited = false;
bool active = false;
uint32_t cycleEnd = 0;

class Callbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice d) override {
    String addr = d.getAddress().toString().c_str();
    uint32_t now = millis();

    std::lock_guard<std::mutex> lock(mtx);
    auto it = std::find_if(devices.begin(), devices.end(),
                           [&](const Device &x) { return x.addr == addr; });
    if (it == devices.end()) {
      if (devices.size() >= MAX_DEVICES) {  // evict the stalest
        auto oldest = std::min_element(devices.begin(), devices.end(),
            [](const Device &a, const Device &b) { return a.lastSeen < b.lastSeen; });
        devices.erase(oldest);
      }
      Device nd;
      nd.addr = addr;
      nd.firstSeen = now;
      devices.push_back(nd);
      it = devices.end() - 1;
    }

    it->rssi = d.getRSSI();
    it->lastSeen = now;
    it->seenCount++;
    it->randomAddr = d.getAddressType() != BLE_ADDR_TYPE_PUBLIC;
    if (d.haveName() && d.getName().length()) it->name = d.getName().c_str();
    if (d.haveManufacturerData()) {
      auto m = d.getManufacturerData();
      if (m.length() >= 2) {
        const uint8_t *p = (const uint8_t *)m.c_str();
        it->hasMfg = true;
        it->mfgId = p[0] | (p[1] << 8);  // company ID, little-endian
      }
    }
    if (d.haveTXPower()) {
      it->hasTx = true;
      it->tx = d.getTXPower();
    }
    if (d.haveServiceUUID()) it->service = d.getServiceUUID().toString().c_str();
  }
};
Callbacks callbacks;

void startCycle() {
  scan->clearResults();
  scan->start(CYCLE_SECS, nullptr, false);  // non-blocking
  cycleEnd = millis() + CYCLE_SECS * 1000 + 300;
}

}  // namespace

bool start() {
  if (!inited) {
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
  }
  if (!active) {
    active = true;
    startCycle();
  }
  return true;
}

void stop() {
  if (!active) return;
  active = false;
  scan->stop();
}

bool scanning() { return active; }

void loop() {
  if (!active) return;
  if ((int32_t)(millis() - cycleEnd) >= 0) startCycle();

  static uint32_t lastPrune = 0;
  if (millis() - lastPrune > 2000) {
    lastPrune = millis();
    uint32_t now = millis();
    std::lock_guard<std::mutex> lock(mtx);
    devices.erase(std::remove_if(devices.begin(), devices.end(),
                                 [&](const Device &d) { return now - d.lastSeen > FORGET_MS; }),
                  devices.end());
  }
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
