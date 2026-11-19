#include <Arduino.h>
#include <lvgl.h>

#include "ble.h"
#include "display.h"
#include "drone.h"
#include "led.h"
#include "net.h"
#include "ota.h"
#include "theme.h"
#include "timesvc.h"
#include "ui.h"
#include "version.h"
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
  Serial.printf("\nCYD UI v%s (built %s %s)\n", FW_VERSION, __DATE__, __TIME__);
  display::begin();
  theme::begin();
  net::begin();
  timesvc::begin();
  weather::begin();
  ui::begin();
}

void loop() {
  lv_timer_handler();
  net::loop();
  ble::loop();
  timesvc::loop();
  weather::loop();
  ota::loop();
  drone::loop();

  if (!bootDone && (networkSettled() || millis() > BOOT_LED_MAX_MS)) {
    bootDone = true;
    led::set(led::Color::Green);
    Serial.printf("[boot] up and running after %lu ms\n", (unsigned long)millis());
  }
  delay(5);
}
