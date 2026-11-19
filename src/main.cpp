#include <Arduino.h>
#include <lvgl.h>

#include "ble.h"
#include "display.h"
#include "net.h"
#include "timesvc.h"
#include "ui.h"
#include "version.h"
#include "weather.h"

void setup() {
  Serial.begin(115200);
  Serial.printf("\nCYD UI v%s (built %s %s)\n", FW_VERSION, __DATE__, __TIME__);
  display::begin();
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
  delay(5);
}
