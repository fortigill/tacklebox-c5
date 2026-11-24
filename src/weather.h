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
  const char *region;  // picker heading; presets are grouped by region in order
};
const BuoyPreset *buoyPresets(size_t &n);  // nearby buoys offered in the picker
String buoyName(const String &id);         // preset name, or "" for other stations
std::vector<String> buoyFavorites();       // IDs typed in under "Other station ID", newest first
void addBuoyFavorite(const String &id);    // no-op for built-in buoys; keeps up to 8
void removeBuoyFavorite(const String &id);
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

// ---- Stock quote (Yahoo Finance chart data, NYSE/NASDAQ and more) ----
// One ticker, refreshed every 5 min while the US market is open (30 min
// otherwise), paused like the other downloads while Bluetooth tools run.
struct Stock {
  bool valid = false;     // has a price (kept from the last good update if a later one failed)
  String symbol, name, exchange, currency;
  String error;           // why the last update of this ticker failed, "" if it worked
  float price = NAN, prevClose = NAN, dayHigh = NAN, dayLow = NAN;
  uint32_t marketTime = 0;  // epoch of the last trade
  uint32_t fetchedMs = 0;
};
// Up to 8 tickers (default SPY), all fetched over one HTTPS session
size_t stockCount();
std::vector<String> stockSymbols();
bool stockAt(size_t i, Stock &out);   // in list order; symbol is set even before data arrives
bool findStock(const String &sym, Stock &out);
std::vector<Stock> stocks();
bool addStockSymbol(const String &sym, String &why);  // false if duplicate or list full
void removeStockSymbol(const String &sym);
bool showStockBar();                  // price + day change in the status bar
void setShowStockBar(bool on);
bool showStockDesk();                 // desktop widget
void setShowStockDesk(bool on);
bool stockActive();                   // either display on, so the quote is polled
void refreshStock();
uint32_t stockVersion();
String stockStatus();
float stockChangePct(const Stock &s);  // day change in %, NAN if unknown

// ---- NFL scores (ESPN scoreboard, this week's games) ----
// Every minute while your team's game is on, every 2 min in the half hour
// before kickoff, every 30 min otherwise. Paused while Bluetooth tools run.
struct Game {
  String shortName;          // "SEA @ SF"
  String away, home;         // abbreviations, e.g. "SEA"
  int awayScore = 0, homeScore = 0;
  String state;              // "pre", "in" or "post"
  String detail;             // "Q3 5:32", "Final", "Sun, 11/23 - 1:00 PM EST"
  String downDist;           // "3rd & 7 at SF 35" while in play
  String possession;         // abbreviation of the team with the ball, or ""
  uint32_t kickoff = 0;      // epoch
};
size_t nflTeamCount();
const char *nflTeamAbbrAt(size_t i);
const char *nflTeamNameAt(size_t i);
String nflTeam();                     // tracked team ("" = none)
void setNflTeam(const String &abbr);
bool showNflBar();
void setShowNflBar(bool on);
bool showNflDesk();
void setShowNflDesk(bool on);
bool nflActive();
void refreshNfl();
std::vector<Game> nflGamesList();
bool nflTeamGame(Game &out);          // your team's game this week
uint32_t nflVersion();
String nflStatus();

// ---- Earthquakes (USGS Earthquake Hazards Program, FDSN event service) ----
// Every quake at or above the chosen magnitude in the last 96 hours,
// worldwide or within a radius of the ZIP code. Refreshed every 10 min.
struct Quake {
  char id[20] = "";        // USGS event ID, e.g. "us7000abcd"
  uint32_t time = 0;       // origin time, epoch UTC
  float mag = NAN;
  char magType[6] = "";    // ml, md, mb, mww...
  float lat = NAN, lon = NAN, depthKm = NAN;
  char net[8] = "";        // contributing network, e.g. "ci", "us"
  char place[48] = "";     // "12 km SW of Ridgecrest, CA"
};
struct QuakeSummary {
  bool valid = false;
  int today = 0, h24 = 0, h48 = 0, h96 = 0;  // counts (today = since local midnight)
  int stored = 0;          // quakes kept for the list (newest first; capped)
  bool haveBiggest = false;
  Quake biggest24;         // largest in the last 24 h
  bool haveLatest = false;
  Quake latest;            // most recent
  uint32_t fetchedMs = 0;
};
QuakeSummary quakeSummary();
std::vector<Quake> quakes(uint32_t sinceEpoch, size_t maxN);  // newest first
float quakeMinMag();                  // 1.0 / 2.5 / 4.5 / 6.0 (default 2.5)
void setQuakeMinMag(float m);
int quakeRadiusKm();                  // 0 = worldwide
void setQuakeRadiusKm(int km);
bool showQuakeDesk();                 // desktop widget (also turns polling on)
void setShowQuakeDesk(bool on);
void refreshQuake();
void quakeKeepAlive();                // the quake page is open: keep polling even with the widget off
uint32_t quakeVersion();
String quakeStatus();
bool quakeDistanceKm(const Quake &q, float &km);  // from the ZIP code; false without one

// Presentation helpers
const lv_img_dsc_t *icon(const Data &d, bool large);
const char *describe(int code);
int temp(float c);          // in the selected unit, rounded
int wind(float kmh);        // mph or km/h by unit
const char *windUnit();

}  // namespace weather
