#pragma once

#include <stdint.h>

namespace ui {
// Builds the desktop and starts the UI refresh timer. Call after display::begin().
void begin();

// OTA progress screen. These run inside ArduinoOTA.handle(), outside the
// normal LVGL timer loop, so they force a redraw themselves.
// Asks "Accept / Decline" with a countdown. Returns true only if
// Accept was tapped before the timeout; no answer counts as a refusal.
bool otaAsk(unsigned sizeKB, uint32_t timeoutMs);
void otaStart();
void otaProgress(int pct);
void otaFinished(bool ok, const char *msg);
}
