#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include <vector>

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
bool tlsActive();          // an HTTPS download is in progress (big heap user)
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

// ---- NWS alerts (api.weather.gov, US only) ----
// Active watches/warnings/advisories covering the ZIP code's location,
// refreshed every 5 minutes while Wi-Fi is up. Alerts drop off when they
// expire. A new alert pops up once and makes the desktop weather icon flash
// until it is snoozed.
struct Alert {
  String id;
  String event;        // e.g. "Winter Storm Warning"
  String headline;
  String severity;     // Extreme / Severe / Moderate / Minor / Unknown
  String urgency, certainty;
  String area;         // counties / zones covered
  String description;  // truncated to keep RAM in check
  String instruction;
  String sender;
  uint32_t onset = 0, until = 0;  // epoch seconds (0 = not given)
  bool snoozed = false;
};

std::vector<Alert> alerts();             // active alerts, most severe first
bool findAlert(const String &id, Alert &out);
void snoozeAlert(const String &id, bool on);  // saved across reboots
size_t alertCount();
size_t unsnoozedAlertCount();            // > 0 = desktop weather icon flashes
bool nextAlertPopup(String &id);         // each new alert is handed out once
uint32_t alertsVersion();                // changes whenever the list changes
String alertsStatus();                   // last update / error, for display
bool showAlertDesk();                    // active-alert count widget on the desktop
void setShowAlertDesk(bool on);
int severityRank(const String &severity);  // 4 Extreme .. 0 Unknown

// ---- NDBC buoy (www.ndbc.noaa.gov, any station id) ----
// Latest observation of one buoy, refreshed every 30 minutes. Missing values
// are NAN (or -1 for directions).
struct Buoy {
  bool valid = false;
  String station;
  uint32_t obsTime = 0;   // newest row, epoch UTC
  uint32_t waveTime = 0;  // row the wave values came from
  float waveM = NAN;      // significant wave height
  float domPeriodS = NAN, avgPeriodS = NAN;
  int waveDirDeg = -1;    // direction waves come from
  int windDirDeg = -1;
  float windMs = NAN, gustMs = NAN;
  float pressHpa = NAN, airC = NAN, waterC = NAN;
  uint32_t fetchedMs = 0;
};
struct BuoyPreset {
  const char *id;
  const char *name;
};
const BuoyPreset *buoyPresets(size_t &n);  // nearby buoys offered in the picker
String buoyName(const String &id);         // preset name, or "" for other stations
String buoyStation();                      // default 41009
void setBuoyStation(const String &id);
bool showBuoyBar();                        // wave height in the status bar
void setShowBuoyBar(bool on);
bool showBuoyDesk();                       // wave height widget on the desktop
void setShowBuoyDesk(bool on);
bool buoyActive();                         // either display is on, so the buoy is polled
void refreshBuoy();
Buoy buoy();
uint32_t buoyVersion();                    // changes when buoy data or status changes
String buoyStatus();
String waveText(float m);                  // 4' 7" (or 1.4 m in Celsius mode)

// Presentation helpers
const lv_img_dsc_t *icon(const Data &d, bool large);
const char *describe(int code);
int temp(float c);          // in the selected unit, rounded
int wind(float kmh);        // mph or km/h by unit
const char *windUnit();

}  // namespace weather
