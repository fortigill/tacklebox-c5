#include <Arduino.h>
#include <esp_system.h>
#include <lvgl.h>

#include "ble.h"
#include "display.h"
#include "drone.h"
#include "led.h"
#include "net.h"
#include "ota.h"
#include "sysmon.h"
#include "theme.h"
#include "timesvc.h"
#include "ui.h"
#include "version.h"
#include "board.h"
#include "weather.h"

// Boot is "done" (LED turns green) once setup() has finished and Wi-Fi has
// settled: connected, failed, or nothing saved to connect to. Capped so a
// slow or missing network can't keep the LED blinking forever.
static const uint32_t BOOT_LED_MAX_MS = 20000;
static bool bootDone = false;

static bool networkSettled() {
  net::State s = net::state();
  return s == net::State::Connected || s == net::State::Failed || net::savedSsid().isEmpty();
}

void setup() {
  // LED first: solid red the moment we're running, then blink while booting
  led::begin();
  led::set(led::Color::Red);
  led::blink(led::Color::Red, 200);

  Serial.begin(115200);
  Serial.printf("\n%s v%s (built %s %s) on %s\n%s\n", PRODUCT_NAME, FW_VERSION, __DATE__, __TIME__, BOARD_NAME,
                PRODUCT_TAGLINE);
  // Why did we (re)start? Helps tell crashes and watchdog resets from power-ons.
  const esp_reset_reason_t rr = esp_reset_reason();
  const char *why = rr == ESP_RST_POWERON  ? "power on"
                    : rr == ESP_RST_SW     ? "software restart (OTA or reboot)"
                    : rr == ESP_RST_PANIC  ? "CRASH (panic)"
                    : rr == ESP_RST_INT_WDT ? "CRASH (interrupt watchdog)"
                    : rr == ESP_RST_TASK_WDT ? "CRASH (task watchdog)"
                    : rr == ESP_RST_WDT    ? "CRASH (watchdog)"
                    : rr == ESP_RST_BROWNOUT ? "brownout (power dipped)"
                    : rr == ESP_RST_EXT    ? "reset button"
                                           : "other";
  Serial.printf("[boot] reset reason: %s (%d)\n", why, (int)rr);
  display::begin();
  theme::begin();
  net::begin();
  timesvc::begin();
  weather::begin();
  drone::begin();
  ui::begin();
  sysmon::begin();
}

void loop() {
  lv_timer_handler();
  net::loop();
  ble::loop();
  timesvc::loop();
  weather::loop();
  ota::loop();
  drone::loop();
  sysmon::loop();

  if (!bootDone && (networkSettled() || millis() > BOOT_LED_MAX_MS)) {
    bootDone = true;
    led::set(led::Color::Green);
    Serial.printf("[boot] up and running after %lu ms; internal RAM %lu KB free (largest block %lu KB), "
                  "PSRAM %lu KB free\n",
                  (unsigned long)millis(), (unsigned long)(ESP.getFreeHeap() / 1024),
                  (unsigned long)(ESP.getMaxAllocHeap() / 1024), (unsigned long)(ESP.getFreePsram() / 1024));
  }
  delay(5);
}
