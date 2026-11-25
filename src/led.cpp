#include "led.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_timer.h>

#include "board.h"

namespace led {
namespace {

#if !defined(BOARD_NM_CYD_C5)
const uint8_t PIN_R = 4;
const uint8_t PIN_G = 16;
const uint8_t PIN_B = 17;
#endif

esp_timer_handle_t timer = nullptr;
volatile Color blinkColor = Color::Off;
volatile bool blinkOn = false;
volatile bool ledEnabled = true;
Color solidColor = Color::Off;  // last solid color, shown again when re-enabled

void write(Color c) {
  if (!ledEnabled) c = Color::Off;
  bool r = c == Color::Red || c == Color::Yellow || c == Color::Magenta || c == Color::White;
  bool g = c == Color::Green || c == Color::Yellow || c == Color::Cyan || c == Color::White;
  bool b = c == Color::Blue || c == Color::Cyan || c == Color::Magenta || c == Color::White;
#if defined(BOARD_NM_CYD_C5)
  // WS2812: a dim level is plenty for a status light and easy on the eyes
  const uint8_t L = 40;
  rgbLedWrite(PIN_RGB_LED, r ? L : 0, g ? L : 0, b ? L : 0);
#else
  digitalWrite(PIN_R, r ? LOW : HIGH);  // active low
  digitalWrite(PIN_G, g ? LOW : HIGH);
  digitalWrite(PIN_B, b ? LOW : HIGH);
#endif
}

void onTimer(void *) {
  blinkOn = !blinkOn;
  write(blinkOn ? blinkColor : Color::Off);
}

void stopTimer() {
  if (timer && esp_timer_is_active(timer)) esp_timer_stop(timer);
}

}  // namespace

void begin() {
#if !defined(BOARD_NM_CYD_C5)
  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);
#endif
  Preferences p;
  if (p.begin("ui", true)) {
    ledEnabled = p.getBool("ledOn", true);
    p.end();
  }
  write(Color::Off);
  esp_timer_create_args_t args = {};
  args.callback = onTimer;
  args.name = "led";
  esp_timer_create(&args, &timer);
}

void set(Color c) {
  stopTimer();
  solidColor = c;
  write(c);
}

void blink(Color c, uint32_t halfPeriodMs) {
  stopTimer();
  blinkColor = c;
  blinkOn = true;
  write(c);
  if (timer) esp_timer_start_periodic(timer, (uint64_t)halfPeriodMs * 1000);
}

bool enabled() { return ledEnabled; }

void setEnabled(bool on) {
  ledEnabled = on;
  Preferences p;
  if (p.begin("ui", false)) {
    p.putBool("ledOn", on);
    p.end();
  }
  if (!(timer && esp_timer_is_active(timer))) write(on ? solidColor : Color::Off);  // a blink picks it up itself
}

}  // namespace led
