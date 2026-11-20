#include "ota.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include "version.h"
#include <Update.h>

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

const char *HOSTNAME = PRODUCT_HOSTNAME;
bool started = false;
int lastPct = -1;
bool refused = false;
// Stock espota.py waits only 10 s for the device to connect back after the
// invitation; the cyd-ota env uses a copy patched to 45 s (tools/espota_patch.py).
const uint32_t ASK_TIMEOUT_MS = 30000;

void begin() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_STR(OTA_PASSWORD));

  ArduinoOTA.onStart([]() {
    Serial.printf("[ota] update offered (%s, %u bytes), asking the user\n",
                  ArduinoOTA.getCommand() == U_FLASH ? "firmware" : "filesystem", (unsigned)Update.size());
    refused = !ui::otaAsk((unsigned)(Update.size() / 1024), ASK_TIMEOUT_MS);
    if (refused) {
      // Nothing has been written yet. Aborting makes every following write
      // fail, so espota reports an error and the running firmware stays.
      Update.abort();
      return;
    }
    ble::stop();
    net::stopHunt();
    lastPct = -1;
    ui::otaStart();
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    if (refused) return;
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
    if (refused) {
      Serial.println("[ota] refused update ended");
      refused = false;
      return;  // the refusal was already handled on screen
    }
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
