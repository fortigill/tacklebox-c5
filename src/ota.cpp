#include "ota.h"

#include <Arduino.h>
#include <ArduinoOTA.h>

#include "ble.h"
#include "net.h"
#include "ui.h"

#ifndef OTA_PASSWORD
#error "Define OTA_PASSWORD (see [ota] in platformio.ini)"
#endif
#define OTA_STR2(x) #x
#define OTA_STR(x) OTA_STR2(x)

namespace ota {
namespace {

const char *HOSTNAME = "cyd";
bool started = false;
int lastPct = -1;

void begin() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_STR(OTA_PASSWORD));

  ArduinoOTA.onStart([]() {
    Serial.printf("[ota] update started (%s)\n",
                  ArduinoOTA.getCommand() == U_FLASH ? "firmware" : "filesystem");
    ble::stop();
    net::stopHunt();
    lastPct = -1;
    ui::otaStart();
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    int pct = total ? (int)((uint64_t)done * 100 / total) : 0;
    if (pct == lastPct) return;  // redraw only when the number changes
    lastPct = pct;
    ui::otaProgress(pct);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("[ota] update complete, rebooting");
    ui::otaFinished(true, "Update complete - restarting...");
  });
  ArduinoOTA.onError([](ota_error_t e) {
    const char *why = e == OTA_AUTH_ERROR      ? "Wrong OTA password"
                      : e == OTA_BEGIN_ERROR   ? "Not enough space for the update"
                      : e == OTA_CONNECT_ERROR ? "Connection failed"
                      : e == OTA_RECEIVE_ERROR ? "Upload interrupted"
                                               : "Update failed";
    Serial.printf("[ota] error %d: %s\n", (int)e, why);
    ui::otaFinished(false, why);
  });

  ArduinoOTA.begin();
  started = true;
  Serial.printf("[ota] ready: %s.local (%s)\n", HOSTNAME, net::ip().c_str());
}

}  // namespace

void loop() {
  if (!started) {
    if (net::state() == net::State::Connected) begin();
    return;
  }
  ArduinoOTA.handle();  // blocks for the duration of an upload, calling the hooks above
}

bool ready() { return started; }
const char *hostname() { return HOSTNAME; }

}  // namespace ota
