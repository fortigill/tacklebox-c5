#pragma once

#include <stdint.h>

// System health: CPU load per core, from FreeRTOS idle hooks.
//
// Each core's idle task calls a hook after waking from an interrupt. The hook
// counts the RTOS ticks (1 ms) in which the idle task got to run at all, so
// load = ticks without idle time / ticks elapsed, over the last second. A tick
// that was mostly busy but idled briefly counts as idle, so the figure reads a
// little low: a good trend, not a profiler.
namespace sysmon {

void begin();               // register the idle hooks
void loop();                // recompute once a second
int cpuLoad(int core);      // 0-100 %, -1 until the first second has passed

}  // namespace sysmon
