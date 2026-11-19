#pragma once

#include <Arduino.h>
#include <lvgl.h>

// Weather from Open-Meteo (free, no API key). The ZIP code is resolved to
// coordinates once (Zippopotam.us, falling back to Open-Meteo geocoding) and
// cached. Fetches run in a short-lived background task so the UI never blocks.
namespace weather {

struct Data {
  bool valid = false;
  float tempC = 0;       // current
  int code = 0;          // current WMO weather code
  int dayCode = 0;       // today's forecast WMO code (drives the icon)
  float hiC = 0, loC = 0;
  float windKmh = 0;     // current
  float windMaxKmh = 0;  // today's max
  int precipPct = 0;     // today's max precipitation probability
  uint32_t fetchedMs = 0;
};

enum class Status { Idle, Busy, Error };

void begin();
void loop();  // schedules refreshes (every 15 min while Wi-Fi is up)

bool configured();         // a ZIP code is set
String zip();
String place();            // e.g. "New York, NY"
void setZip(const String &zip);  // clears the cached location and refetches
void refresh();

bool useF();
void setUseF(bool f);

bool showIcon();            // show the forecast icon in the desktop status bar
void setShowIcon(bool on);

Data data();
Status status();
String lastError();

// Presentation helpers
const lv_img_dsc_t *icon(const Data &d, bool large);
const char *describe(int code);
int temp(float c);          // in the selected unit, rounded
int wind(float kmh);        // mph or km/h by unit
const char *windUnit();

}  // namespace weather
