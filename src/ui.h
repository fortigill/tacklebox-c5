#pragma once

namespace ui {
// Builds the desktop and starts the UI refresh timer. Call after display::begin().
void begin();

// OTA progress screen. These run inside ArduinoOTA.handle(), outside the
// normal LVGL timer loop, so they force a redraw themselves.
void otaStart();
void otaProgress(int pct);
void otaFinished(bool ok, const char *msg);
}
