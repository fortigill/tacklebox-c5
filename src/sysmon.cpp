#include "sysmon.h"

#include <Arduino.h>
#include <esp_freertos_hooks.h>

namespace sysmon {
namespace {

#if CONFIG_FREERTOS_UNICORE
const int NCORES = 1;  // e.g. ESP32-C5: core 1 stays at -1
#else
const int NCORES = 2;
#endif
volatile uint32_t idleTicks[2] = {0, 0};
volatile TickType_t lastTick[2] = {0, 0};
int load[2] = {-1, -1};
TickType_t windowStart = 0;
uint32_t idleAtStart[2] = {0, 0};

// Called by each core's idle task. Returning true means "once per wake-up",
// so the core still sleeps (WFI) between interrupts instead of spinning.
template <int CORE>
bool idleHook() {
  TickType_t t = xTaskGetTickCount();
  if (t != lastTick[CORE]) {
    lastTick[CORE] = t;
    idleTicks[CORE]++;
  }
  return true;
}

}  // namespace

void begin() {
  esp_register_freertos_idle_hook_for_cpu(idleHook<0>, 0);
#if !CONFIG_FREERTOS_UNICORE
  esp_register_freertos_idle_hook_for_cpu(idleHook<1>, 1);
#endif
  windowStart = xTaskGetTickCount();
}

void loop() {
  TickType_t now = xTaskGetTickCount();
  TickType_t span = now - windowStart;
  if (span < pdMS_TO_TICKS(1000)) return;
  for (int c = 0; c < NCORES; c++) {
    uint32_t idle = idleTicks[c] - idleAtStart[c];
    idleAtStart[c] = idleTicks[c];
    if (idle > span) idle = span;
    load[c] = 100 - (int)(idle * 100 / span);
  }
  windowStart = now;
}

int cpuLoad(int core) { return (core == 0 || core == 1) ? load[core] : -1; }

}  // namespace sysmon
