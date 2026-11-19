#include "led.h"

#include <Arduino.h>
#include <esp_timer.h>

namespace led {
namespace {

const uint8_t PIN_R = 4;
const uint8_t PIN_G = 16;
const uint8_t PIN_B = 17;

esp_timer_handle_t timer = nullptr;
volatile Color blinkColor = Color::Off;
volatile bool blinkOn = false;

void write(Color c) {
  bool r = c == Color::Red || c == Color::Yellow || c == Color::Magenta || c == Color::White;
  bool g = c == Color::Green || c == Color::Yellow || c == Color::Cyan || c == Color::White;
  bool b = c == Color::Blue || c == Color::Cyan || c == Color::Magenta || c == Color::White;
  digitalWrite(PIN_R, r ? LOW : HIGH);  // active low
  digitalWrite(PIN_G, g ? LOW : HIGH);
  digitalWrite(PIN_B, b ? LOW : HIGH);
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
  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  write(Color::Off);
  esp_timer_create_args_t args = {};
  args.callback = onTimer;
  args.name = "led";
  esp_timer_create(&args, &timer);
}

void set(Color c) {
  stopTimer();
  write(c);
}

void blink(Color c, uint32_t halfPeriodMs) {
  stopTimer();
  blinkColor = c;
  blinkOn = true;
  write(c);
  if (timer) esp_timer_start_periodic(timer, (uint64_t)halfPeriodMs * 1000);
}

}  // namespace led
