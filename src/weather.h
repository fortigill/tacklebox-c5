#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include <vector>

#include "board.h"
#include "psram_vec.h"

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
// Every quake at or above the chosen magnitude in the last 24 hours,
// worldwide or within a radius of the ZIP code, plus today's count and
// yesterday's total (local calendar days). Refreshed every 10 min.
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
  int today = 0, h24 = 0;  // counts (today = since local midnight)
  long yesterday = -1;     // total for yesterday (midnight to midnight), -1 = unknown
  int stored = 0;          // quakes kept for the list (newest first; capped)
  bool haveBiggest = false;
  Quake biggest24;         // largest in the last 24 h
  bool haveLatest = false;
  Quake latest;            // most recent
  uint32_t fetchedMs = 0;
};
QuakeSummary quakeSummary();
BigVec<Quake> quakes(uint32_t sinceEpoch, size_t maxN);  // newest first (last 24 h only)
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

// ---- Volcanoes ----
// U.S. volcanoes at an elevated alert level (USGS Volcano Hazards Program,
// HANS API), checked every 15 min, and the worldwide Smithsonian / USGS
// Weekly Volcanic Activity Report (RSS), checked every 6 hours.
struct Volcano {           // one elevated U.S. volcano
  char name[32] = "";
  char vnum[8] = "";       // Smithsonian volcano number
  char alert[12] = "";     // NORMAL / ADVISORY / WATCH / WARNING
  char color[12] = "";     // GREEN / YELLOW / ORANGE / RED
  char obs[40] = "";       // e.g. "Alaska Volcano Observatory"
  uint32_t sent = 0;       // latest notice, epoch UTC
  char synopsis[200] = ""; // short notice summary, when USGS gives one
};
struct Eruption {          // one volcano in the weekly worldwide report
  char name[32] = "";
  char country[28] = "";
  bool isNew = false;      // new activity this week (vs. continuing)
  float lat = NAN, lon = NAN;
#if defined(BOARD_NM_CYD_C5)
  char summary[420] = "";
#else
  char summary[100] = "";  // RAM is tight on the classic board
#endif
};
std::vector<Volcano> usVolcanoes();     // most serious first
BigVec<Eruption> eruptions();           // in report order
String eruptionWeek();                  // "19 November-25 November 2025", "" if unknown
int colorRank(const char *color);       // RED 4, ORANGE 3, YELLOW 2, GREEN 1, else 0
bool showVolcanoDesk();
void setShowVolcanoDesk(bool on);
bool volcanoPopups();                   // pop up when a U.S. alert level changes
void setVolcanoPopups(bool on);
bool nextVolcanoPopup(String &title, String &msg);  // each change is handed out once
void refreshVolcano();
void volcanoKeepAlive();                // the page is open: keep polling with the widget off
uint32_t volcanoVersion();
String volcanoStatus();                 // U.S. part
String eruptionStatus();                // worldwide part
bool volcanoDistanceKm(float vlat, float vlon, float &km);  // from the ZIP code

// ---- Aircraft near an airport (live ADS-B from adsb.fi) ----
// Every ~30 s: how many aircraft are within 10 NM of one airport, and the
// airliners among them (airline, callsign, flight number, type, distance,
// height, climb/descent, speed), each with its route (origin / destination)
// looked up by callsign on adsbdb.com. The airport's position comes from
// AviationWeather.gov (NOAA).
struct LiveFlight {       // an airliner near the airport right now
  char callsign[10] = ""; // radio callsign, e.g. "AAY2871"
  char flight[10] = "";
  char airline[28] = "";
  char type[6] = "";
  float distNm = 0;
  int32_t altFt = 0;      // above the field (0 on the ground)
  int16_t gsKt = 0;
  int16_t rateFpm = 0;    // climb (+) / descent (-)
  uint8_t phase = 0;      // 0 nearby, 1 approaching, 2 departing, 3 on the ground
  // Route from the adsbdb.com callsign database ("" when unknown)
  bool routeKnown = false;
  char from[5] = "", to[5] = "";            // airport codes (IATA, or ICAO without one)
  char fromCity[24] = "", toCity[24] = "";
  uint8_t ours = 0;       // 1 = flying TO this airport, 2 = FROM it, 0 = passing / unknown
};
std::vector<LiveFlight> liveFlights();        // nearest first
String airportCode();                 // e.g. "DAB"
String airportName();                 // e.g. "DAYTONA BEACH INTL" ("" until looked up)
void setAirportCode(const String &code);  // 3-letter FAA/IATA or 4-letter ICAO
bool showNearbyDesk();                // desktop widget: aircraft near the airport, airliner details
void setShowNearbyDesk(bool on);
int flightsNearTotal();               // all aircraft within 10 NM at the last check (-1 = none yet)
bool showFlightBar();                 // status bar: plane icon with the airliner count
void setShowFlightBar(bool on);
bool showOurFlightsDesk();            // widget for airliners flying to / from this airport (hidden when none)
void setShowOurFlightsDesk(bool on);
void refreshFlights();
void flightsKeepAlive();              // the page is open: keep polling with the widget off
uint32_t flightsVersion();
String flightsStatus();

// ---- Breaking news (RSS) ----
// Latest headlines from one source, refreshed every 10 min:
//   0 AP (AP's official Flipboard feed), 1 Fox News (foxnews.com feed),
//   2 CNN (Google News, CNN stories only: CNN's own feeds stopped in 2024).
struct NewsItem {
  uint32_t time = 0;        // published, epoch UTC (0 = unknown)
  char title[160] = "";
#if defined(BOARD_NM_CYD_C5)
  char summary[320] = "";   // short description when the feed has one
  char link[128] = "";
#endif
};
const int NEWS_SOURCES = 3;
const char *newsSourceName(int src);     // "AP News", "Fox News", "CNN"
int newsSource();
void setNewsSource(int src);             // clears the list and refetches
BigVec<NewsItem> newsItems();            // newest first, up to 20
bool showNewsDesk();
void setShowNewsDesk(bool on);
void refreshNews();
void newsKeepAlive();                    // the news page is open: keep polling with the widget off
uint32_t newsVersion();
String newsStatus();

// ---- Outages (OutageDeck, outagedeck.com) ----
// Every 5 minutes, one list of the incidents active right now across the
// ~180 services OutageDeck follows (from each company's own status page),
// worst first. Filter by minimum severity and, optionally, by provider.
struct Outage {
  char provider[24] = "";  // OutageDeck slug ("discord")
  char name[28] = "";      // "Discord"
  char title[100] = "";    // "Voice connection issues"
  char state[16] = "";     // investigating / identified / monitoring
  uint8_t level = 1;       // 1 minor, 2 major, 3 critical
  uint32_t since = 0;      // startedAt, epoch UTC
};
BigVec<Outage> outages();                 // shown incidents (after the filters), worst first
int outageActiveTotal();                  // active incidents across all providers (-1 = not yet)
int outageMinLevel();                     // 1 minor+, 2 major+, 3 critical only
void setOutageMinLevel(int lvl);
String outageProviders();                 // "" = all providers, else comma-separated slugs
void setOutageProviders(const String &csv);
bool showOutageDesk();
void setShowOutageDesk(bool on);
void refreshOutages();
void outageKeepAlive();
uint32_t outageVersion();
String outageStatus();

// ---- Email (IMAP over TLS) ----
// Signs in to your IMAP server every 5 minutes, read-only (EXAMINE), and
// counts unread mail received today plus the total unread in the inbox, with
// sender and subject of the newest few unread from today. The server's
// certificate is pinned on first use (SHA-256 fingerprint): if it ever
// changes, the board won't send your password until you confirm.
struct MailMsg {
  uint32_t time = 0;      // Date header, epoch UTC (0 = unknown)
  char from[40] = "";     // display name, or the address
  char subject[120] = "";
};
struct MailStatus {
  bool valid = false;
  int unreadToday = 0, unreadTotal = 0;
  int shown = 0;          // entries in msgs
  MailMsg msgs[5];        // newest unread from today first
  uint32_t fetchedMs = 0;
};
String mailHost();
uint16_t mailPort();
String mailUser();
bool mailHasPassword();
void setMailServer(const String &host, uint16_t port);  // forgets the pinned certificate
void setMailUser(const String &user);
void setMailPassword(const String &pass);
bool mailConfigured();
MailStatus mailStatus();
String mailStatusText();
String mailCertFingerprint();           // "AB:CD:..." of the pinned certificate ("" if none)
bool mailCertChanged(String &newFp);    // server presented a different certificate: waiting for you
void trustMailCert();                   // accept the new certificate
void forgetMailCert();                  // trust whatever the server presents next time
bool showMailDesk();
void setShowMailDesk(bool on);
bool showMailBar();                     // status bar: envelope with today's unread count
void setShowMailBar(bool on);
void refreshMail();
void mailKeepAlive();
uint32_t mailVersion();

// Presentation helpers
const lv_img_dsc_t *icon(const Data &d, bool large);
const char *describe(int code);
int temp(float c);          // in the selected unit, rounded
int wind(float kmh);        // mph or km/h by unit
const char *windUnit();

}  // namespace weather
