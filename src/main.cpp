#include <Arduino.h>
#include <lvgl.h>

#include "display.h"
#include "net.h"
#include "ui.h"

void setup() {
  Serial.begin(115200);
  display::begin();
  net::begin();
  ui::begin();
}

void loop() {
  lv_timer_handler();
  net::loop();
  delay(5);
}
