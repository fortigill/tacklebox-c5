#include "weather.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <esp_heap_caps.h>
#if defined(BOARD_NM_CYD_C5)
#include "mbedtls/platform.h"
#endif

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>

#include "ble.h"
#include "board.h"
#include "drone.h"
#include "net.h"
#include "version.h"
#include "wx_icons.h"

namespace weather {
namespace {

const uint32_t REFRESH_MS = 15UL * 60 * 1000;  // normal refresh
const uint32_t RETRY_MS = 2UL * 60 * 1000;     // after a failure
const uint32_t TASK_STACK = 12288;  // room for a TLS handshake (NWS is HTTPS-only)
const uint32_t ALERTS_REFRESH_MS = 5UL * 60 * 1000;
const size_t MAX_ALERTS = 8;
const size_t MAX_DESC = 900, MAX_INSTR = 450;
const size_t MAX_SNOOZED = 16;
const uint32_t ALERTS_BOOT_DELAY_MS = 30000;  // first NWS check waits until boot has settled
// Skip HTTPS when RAM is too tight: a TLS session takes ~50 KB in total, with
// single allocations up to ~17 KB. A 38 KB block with 47 KB free still failed.
const uint32_t TLS_MIN_BLOCK = 36 * 1024;
const uint32_t TLS_MIN_FREE = 54 * 1024;
bool tlsInPsram = false;  // C5: mbedTLS allocates from PSRAM, so TLS barely touches internal RAM
bool tlsFits(String &why) {
  uint32_t minBlock = tlsInPsram ? 8 * 1024 : TLS_MIN_BLOCK, minFree = tlsInPsram ? 24 * 1024 : TLS_MIN_FREE;
  if (ESP.getMaxAllocHeap() >= minBlock && ESP.getFreeHeap() >= minFree) return true;
  why = String("skipped, low memory (") + ESP.getFreeHeap() / 1024 + " KB free, " + ESP.getMaxAllocHeap() / 1024 +
        " KB block)";
#if !defined(BOARD_NM_CYD_C5)
  why += ". Bluetooth tools hold memory; it is released 1 min after they stop";
#endif
  return false;
}
const uint16_t HTTP_TIMEOUT_MS = 8000;
const char *ALERTS_TMP = "/nws.json";          // alert download is saved here, then parsed offline
const size_t ALERTS_MAX_BYTES = 96 * 1024;     // fits the 128 KB SPIFFS partition
const uint32_t FULLTEXT_MIN_BLOCK = 48 * 1024; // keep the long alert texts only with this much RAM
// NDBC buoys: waves are reported about hourly, so every 30 min is plenty
const uint32_t BUOY_REFRESH_MS = 30UL * 60 * 1000;
const uint32_t BUOY_RETRY_MS = 5UL * 60 * 1000;
const uint32_t BUOY_BOOT_DELAY_MS = 45000;  // after the first forecast and alert checks
const size_t BUOY_READ_BYTES = 8192;        // newest rows of a ~600 KB file are enough
const char *BUOY_DEFAULT = "41009";         // Canaveral, 20 NM east of Cape Canaveral
// Stock quote (Yahoo Finance chart data, HTTPS only)
const uint32_t STOCK_REFRESH_MS = 5UL * 60 * 1000;   // while the US market is open
const uint32_t STOCK_CLOSED_MS = 30UL * 60 * 1000;   // nights and weekends
const uint32_t STOCK_RETRY_MS = 2UL * 60 * 1000;
const uint32_t STOCK_BOOT_DELAY_MS = 60000;          // after forecast, alerts and buoy
const char *STOCK_DEFAULT = "SPY";
// NFL scores (ESPN scoreboard)
const uint32_t NFL_LIVE_MS = 60UL * 1000;          // your team's game is on
const uint32_t NFL_SOON_MS = 2UL * 60 * 1000;      // kickoff within 30 min
const uint32_t NFL_IDLE_MS = 30UL * 60 * 1000;     // otherwise
const uint32_t NFL_RETRY_MS = 2UL * 60 * 1000;
const uint32_t NFL_BOOT_DELAY_MS = 75000;
const size_t MAX_GAMES = 20;
// Earthquakes (USGS FDSN event service, text format)
const uint32_t QUAKE_REFRESH_MS = 10UL * 60 * 1000;
const uint32_t QUAKE_RETRY_MS = 3UL * 60 * 1000;
const uint32_t QUAKE_BOOT_DELAY_MS = 90000;
const uint32_t QUAKE_WANTED_MS = 15UL * 60 * 1000;  // keep polling this long after the page was open
const uint32_t QUAKE_WINDOW_S = 24UL * 3600;  // the list covers the last 24 h
const char *QUAKE_HOST = "earthquake.usgs.gov/fdsnws/event/1/";
// Volcanoes: USGS HANS (U.S. alert levels) + Smithsonian weekly report (world)
const uint32_t VOLC_REFRESH_MS = 15UL * 60 * 1000;
const uint32_t VOLC_RETRY_MS = 5UL * 60 * 1000;
const uint32_t VOLC_BOOT_DELAY_MS = 105000;
const uint32_t ERUPT_REFRESH_MS = 6UL * 3600 * 1000;  // the report changes once a week
const uint32_t ERUPT_RETRY_MS = 30UL * 60 * 1000;
const char *VOLC_ELEVATED = "volcanoes.usgs.gov/hans-public/api/volcano/getElevatedVolcanoes";
const char *VOLC_CAP = "volcanoes.usgs.gov/hans-public/api/volcano/getCapElevated";  // adds a synopsis
const char *ERUPT_RSS = "volcano.si.edu/news/WeeklyVolcanoRSS.xml";
const size_t VOLC_MAX = 16;
// Airport flights: live ADS-B (adsb.fi open data) + airport position (AviationWeather.gov)
#if defined(BOARD_NM_CYD_C5)
const uint32_t FLT_POLL_MS = 30000;
#else
const uint32_t FLT_POLL_MS = 60000;  // fewer HTTPS sessions on the classic board
#endif
const uint32_t FLT_RETRY_MS = 60000;
const uint32_t FLT_BOOT_DELAY_MS = 120000;
const int FLT_RADIUS_NM = 10;
const size_t FLT_KEEP = 10;
const size_t FLT_TRACKS = 40;
const char *ADSB_HOST = "opendata.adsb.fi/api/";
#if defined(BOARD_NM_CYD_C5)
const size_t ERUPT_MAX = 40;
#else
const size_t ERUPT_MAX = 20;
#endif

#if defined(BOARD_NM_CYD_C5)
const size_t QUAKE_KEEP = 200;  // plenty of RAM (a busy M1.0+ day can pass this)
#else
const size_t QUAKE_KEEP = 30;   // ~3 KB; the classic board needs its heap for TLS
#endif
const size_t MAX_STOCKS = 8;

std::mutex mtx;
Preferences prefs;

// Settings + cached location (guarded by mtx)
String zipCode;
String placeName;
float lat = 0, lon = 0;
bool haveLoc = false;
bool unitF = true;
bool iconOn = true;

// Results
Data cur;
volatile Status st = Status::Idle;
String err;

// NWS alerts (guarded by mtx)
std::vector<Alert> alertList;
std::vector<String> snoozedIds;  // persisted
std::vector<String> poppedIds;   // alerts already announced this boot
std::vector<String> popupQueue;
uint32_t alertsVer = 0;
uint32_t alertsFetchedMs = 0;
String alertsErr;

// NDBC buoy (guarded by mtx)
String buoyId;
std::vector<String> buoyFavs;  // station IDs typed in by the user, newest first (persisted)
bool buoyBar = true;    // wave height in the status bar
bool buoyDesk = true;   // wave height widget on the desktop
bool alertDesk = true;  // active-alert count widget on the desktop
Buoy buoyData;
String buoyErr;
uint32_t buoyVer = 0;

// Stock quote (guarded by mtx)
std::vector<String> stockSyms;  // tickers in display order (persisted)
std::vector<Stock> stockList;   // same order as stockSyms; valid=false until fetched
bool stockBar = true, stockDesk = true;
String stockErr;                // last failure of the whole update (memory, TLS...)
uint32_t stockFetchedMs = 0;
uint32_t stockVer = 0;

// NFL (guarded by mtx)
String nflTeamAbbr;                // tracked team, e.g. "SEA" ("" = none)
bool nflBar = true, nflDesk = true;
std::vector<Game> nflGames;
String nflErr;
uint32_t nflFetchedMs = 0;
uint32_t nflVer = 0;

// Earthquakes (guarded by mtx)
float quakeMin = 2.5f;
int quakeRadius = 0;      // km, 0 = worldwide
bool quakeDesk = true;
BigVec<Quake> quakeList;  // newest first, up to QUAKE_KEEP (PSRAM on the C5)
QuakeSummary quakeSum;
String quakeErr;
uint32_t quakeVer = 0;
uint32_t quakeWantedMs = 0;
bool quakeTls = false;           // USGS redirected plain HTTP to HTTPS: go straight there next time

// Volcanoes (guarded by mtx)
std::vector<Volcano> volcList;
BigVec<Eruption> eruptList;
String eruptWeek;
bool volcDesk = true, volcPop = true;
String volcErr, eruptErr;
uint32_t volcFetchedMs = 0, eruptFetchedMs = 0;
bool eruptAttempted = false, eruptLastOk = false;
uint32_t eruptLastAttempt = 0;
uint32_t volcVer = 0;
uint32_t volcWantedMs = 0;
bool volcBaseline = false;            // first U.S. list this boot: no pop-ups for it
std::vector<String> volcPopQueue;     // "title\nmessage"
bool volcTls = false, eruptTls = false;

// Airport flights (guarded by mtx unless noted)
String apCode = "DAB", apName = "DAYTONA BEACH INTL";
float apLat = 29.1799f, apLon = -81.0581f;  // DAB built in; others are looked up
int apElevFt = 34;
float apMagVar = -6.0f;                     // degrees, east positive (DAB ~6 W)
bool apHaveLoc = true;
bool fltDesk = true;
bool nearDesk = true;                       // "near the airport" widget
int fltNearTotal = -1;                      // all aircraft within the radius at the last check
std::vector<FlightEvent> fltDeps, fltArrs;  // newest first
std::vector<LiveFlight> fltLive;
int fltDepToday = 0, fltArrToday = 0;
uint32_t fltDay = 0;                        // local yyyymmdd the counts belong to
String fltErr;
uint32_t fltFetchedMs = 0;
uint32_t fltVer = 0;
uint32_t fltWantedMs = 0;
volatile bool apDirty = false;              // task resolved a new airport: save it from loop()
bool adsbTls = false, avwxTls = false;
int adsbApi = 3;                            // adsb.fi API version (v3, falls back to v2)

// Scheduling (main task only)
uint32_t lastAttempt = 0;
bool attempted = false;
bool lastOk = false;
bool force = false;
uint32_t alLastAttempt = 0;
bool alAttempted = false;
bool alLastOk = false;
bool alForce = false;
uint32_t byLastAttempt = 0;
bool byAttempted = false;
bool byLastOk = false;
bool byForce = false;
uint32_t stLastAttempt = 0;
bool stAttempted = false;
bool stLastOk = false;
bool stForce = false;
uint32_t nfLastAttempt = 0;
bool nfAttempted = false;
bool nfLastOk = false;
bool nfForce = false;
uint32_t flLastAttempt = 0;
bool flAttempted = false;
bool flLastOk = false;
bool flForce = false;
uint32_t vcLastAttempt = 0;
bool vcAttempted = false;
bool vcLastOk = false;
bool vcForce = false;
uint32_t qkLastAttempt = 0;
bool qkAttempted = false;
bool qkLastOk = false;
bool qkForce = false;
volatile bool taskRunning = false;
volatile bool tlsBusy = false;  // an HTTPS session is open (holds ~40 KB of heap)
struct TlsGuard {               // marks the scope of a TLS session for the UI
  TlsGuard() { tlsBusy = true; }
  ~TlsGuard() { tlsBusy = false; }
};
enum : uint8_t { DO_FORECAST = 1, DO_ALERTS = 2, DO_BUOY = 4, DO_STOCK = 8, DO_NFL = 16, DO_QUAKE = 32, DO_VOLCANO = 64, DO_FLIGHTS = 128 };

bool bluetoothBusy() { return drone::running() || ble::stackActive(); }
const char *PAUSED_MSG = "Paused while the drone scanner or a Bluetooth tool is in use. Resumes 1 min after it stops.";
uint8_t taskFlags = 0;
volatile bool locDirty = false;  // task resolved a location; save it from loop()

// Plain HTTP first (no TLS = far less RAM); fall back to HTTPS if the
// server refuses or redirects.
bool httpGet(const String &hostAndPath, String &body, String &why) {
  for (int secure = 0; secure < 2; secure++) {
    // Declaration order matters: locals are destroyed in reverse, and
    // ~HTTPClient() calls stop() on its client. The clients must outlive
    // `http`, or that stop() runs on a destroyed object and corrupts lwIP
    // (assert in pbuf_free).
    WiFiClient plain;
    WiFiClientSecure tls;
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    bool ok;
    std::unique_ptr<TlsGuard> guard;
    if (secure) {
      guard.reset(new TlsGuard());
      tls.setInsecure();  // public, read-only data; skip CA bundle to save RAM
      ok = http.begin(tls, "https://" + hostAndPath);
    } else {
      ok = http.begin(plain, "http://" + hostAndPath);
    }
    if (!ok) {
      why = "bad URL";
      continue;
    }
    int code = http.GET();
    if (code == 200) {
      body = http.getString();
      http.end();
      return true;
    }
    why = code < 0 ? http.errorToString(code) : String("HTTP ") + code;
    http.end();
  }
  return false;
}

bool geocode(const String &zip, float &outLat, float &outLon, String &outPlace, String &why) {
  String body;
  JsonDocument doc;

  bool usZip = zip.length() == 5;
  for (unsigned i = 0; i < zip.length(); i++) usZip = usZip && isDigit(zip[i]);
  if (usZip && httpGet("api.zippopotam.us/us/" + zip, body, why) &&
      !deserializeJson(doc, body)) {
    JsonObject p = doc["places"][0];
    if (!p.isNull()) {
      outLat = atof(p["latitude"] | "0");
      outLon = atof(p["longitude"] | "0");
      outPlace = String(p["place name"] | "") + ", " + String(p["state abbreviation"] | "");
      return true;
    }
  }

  if (httpGet("geocoding-api.open-meteo.com/v1/search?count=1&format=json&name=" + zip, body,
              why) &&
      !deserializeJson(doc, body)) {
    JsonObject r = doc["results"][0];
    if (!r.isNull()) {
      outLat = r["latitude"].as<float>();
      outLon = r["longitude"].as<float>();
      outPlace = String(r["name"] | "");
      const char *admin = r["admin1"] | "";
      if (*admin) outPlace += String(", ") + admin;
      return true;
    }
  }
  why = "ZIP code not found";
  return false;
}

bool fetchForecast(float la, float lo, Data &out, String &why) {
  char path[384];
  snprintf(path, sizeof(path),
           "api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,weather_code,wind_speed_10m"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,wind_speed_10m_max,"
           "precipitation_probability_max"
           "&timezone=auto&forecast_days=1",
           la, lo);
  String body;
  if (!httpGet(path, body, why)) return false;

  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body);
  if (e) {
    why = String("bad data: ") + e.c_str();
    return false;
  }
  JsonObject c = doc["current"];
  JsonObject d = doc["daily"];
  if (c.isNull() || d.isNull()) {
    why = "unexpected response";
    return false;
  }
  out.tempC = c["temperature_2m"].as<float>();
  out.code = c["weather_code"].as<int>();
  out.windKmh = c["wind_speed_10m"].as<float>();
  out.dayCode = d["weather_code"][0].as<int>();
  out.hiC = d["temperature_2m_max"][0].as<float>();
  out.loC = d["temperature_2m_min"][0].as<float>();
  out.windMaxKmh = d["wind_speed_10m_max"][0].as<float>();
  out.precipPct = d["precipitation_probability_max"][0] | 0;
  out.fetchedMs = millis();
  out.valid = true;
  return true;
}

// "2026-11-19T10:00:00-05:00" -> epoch seconds (0 if unparseable)
uint32_t parseIso(const char *s) {
  int Y, M, D, h, m, sec;
  if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec) != 6) return 0;
  const char *tz = s + 19;
  while (*tz == '.' || isdigit((unsigned char)*tz)) tz++;  // fractional seconds
  long off = 0;
  if (*tz == '+' || *tz == '-') {
    int oh = 0, om = 0;
    sscanf(tz + 1, "%d:%d", &oh, &om);
    off = (oh * 3600L + om * 60L) * (*tz == '-' ? -1 : 1);
  }
  // days since 1970-01-01 (civil-from-days inverse, valid for any Gregorian date)
  int y = Y - (M <= 2);
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (M + (M > 2 ? -3 : 9)) + 2) / 5 + D - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097 + doe - 719468;
  long t = days * 86400L + h * 3600L + m * 60L + sec - off;
  return t > 0 ? (uint32_t)t : 0;
}

String clip(const char *s, size_t max) {
  String out(s ? s : "");
  out.replace("\r", "");
  if (out.length() > max) out = out.substring(0, max - 3) + "...";
  return out;
}

bool fsReady() {
  static int8_t state = -1;  // -1 untried, 0 failed, 1 mounted
  if (state < 0) state = SPIFFS.begin(true) ? 1 : 0;  // formats once on first use
  return state == 1;
}

// Step 1: save the NWS response to flash. The TLS session lives only inside this
// function, so its ~40 KB is free again before any JSON parsing starts.
// Copy buffer for downloads, reserved at boot: HTTPClient::writeToStream()
// mallocs its own buffer mid-download, which failed ("too less ram") once the
// TLS session had fragmented the heap. Only the weather task uses it.
uint8_t dlBuf[1024];

// Streams an HTTP body into a file, decoding chunked transfer encoding.
// Returns the number of bytes written, or -1 (with `why` set).
long streamBody(HTTPClient &http, bool chunked, File &f, size_t maxBytes, String &why) {
  Client *in = http.getStreamPtr();
  if (!in) {
    why = "no connection";
    return -1;
  }
  const int total = http.getSize();
  size_t written = 0;
  uint32_t last = millis();
  auto stalled = [&]() { return millis() - last > 15000; };
  auto readByte = [&]() -> int {
    while (true) {
      int c = in->read();
      if (c >= 0) {
        last = millis();
        return c;
      }
      if ((!in->connected() && !in->available()) || stalled()) return -1;
      delay(2);
    }
  };
  // Copies n bytes, or until the server closes when n == SIZE_MAX
  auto copy = [&](size_t n) -> bool {
    while (n) {
      int avail = in->available();
      if (avail <= 0) {
        if (!in->connected()) {
          if (n == SIZE_MAX) return true;
          why = "connection closed early";
          return false;
        }
        if (stalled()) {
          why = "connection stalled";
          return false;
        }
        delay(5);
        continue;
      }
      size_t want = std::min(std::min((size_t)avail, sizeof(dlBuf)), n);
      int r = in->read(dlBuf, want);
      if (r <= 0) continue;
      last = millis();
      if (written + r > maxBytes) {
        why = String("too large (over ") + maxBytes / 1024 + " KB)";
        return false;
      }
      if (f.write(dlBuf, r) != (size_t)r) {
        why = "flash write failed";
        return false;
      }
      written += r;
      if (n != SIZE_MAX) n -= r;
    }
    return true;
  };

  if (chunked) {
    while (true) {
      char line[20];
      size_t k = 0;
      while (true) {  // "<hex size>[;ext]\r\n"
        int c = readByte();
        if (c < 0) {
          why = "connection lost in chunk header";
          return -1;
        }
        if (c == '\n') break;
        if (c != '\r' && k < sizeof(line) - 1) line[k++] = (char)c;
      }
      line[k] = 0;
      unsigned long sz = strtoul(line, nullptr, 16);
      if (!sz) {  // last chunk: skip optional trailers up to the blank line, so a
                  // kept-alive connection starts the next reply cleanly
        for (int guardLines = 0; guardLines < 16; guardLines++) {
          size_t len = 0;
          int c;
          while ((c = readByte()) >= 0 && c != '\n')
            if (c != '\r') len++;
          if (c < 0 || len == 0) break;
        }
        break;
      }
      if (!copy(sz)) return -1;
      readByte();  // CRLF after the chunk data
      readByte();
    }
  } else if (!copy(total > 0 ? (size_t)total : SIZE_MAX)) {
    return -1;
  }
  return (long)written;
}

bool downloadAlerts(float la, float lo, String &why) {
  TlsGuard guard;        // declared first, so it is released after the TLS session is gone
  WiFiClientSecure tls;  // declared before `http` so it outlives it
  HTTPClient http;
  tls.setInsecure();  // public, read-only data
  http.setTimeout(15000);
  http.setConnectTimeout(15000);
  // HTTP/1.1 like a browser: api.weather.gov's CDN answered HTTP/1.0 requests
  // slowly or not at all ("read Timeout"). Chunked replies are decoded by
  // streamBody().
  http.setReuse(false);  // ask the server to close when done
  char url[128];
  snprintf(url, sizeof(url), "https://api.weather.gov/alerts/active?point=%.4f,%.4f", la, lo);
  Serial.printf("[wx] NWS: before TLS free %lu, largest block %lu\n", (unsigned long)ESP.getFreeHeap(),
                (unsigned long)ESP.getMaxAllocHeap());
  if (!http.begin(tls, url)) {
    why = "bad URL";
    return false;
  }
  http.setUserAgent(PRODUCT_UA " (ESP32 touch display; personal weather widget)");
  http.addHeader("Accept", "application/geo+json");
  const char *hdrs[] = {"Transfer-Encoding"};
  http.collectHeaders(hdrs, 1);
  const uint32_t t0 = millis();
  int code = http.GET();
  Serial.printf("[wx] NWS replied %d after %lu ms, size %d; free %lu, largest block %lu, min ever %lu\n",
                code, (unsigned long)(millis() - t0), http.getSize(), (unsigned long)ESP.getFreeHeap(),
                (unsigned long)ESP.getMaxAllocHeap(), (unsigned long)ESP.getMinFreeHeap());
  if (code != 200) {
    why = code < 0 ? http.errorToString(code) : String("HTTP ") + code;
    http.end();
    return false;
  }
  File f = SPIFFS.open(ALERTS_TMP, FILE_WRITE);
  if (!f) {
    why = "can't write to flash";
    http.end();
    return false;
  }
  const int expected = http.getSize();  // -1 for chunked replies
  if (expected > (int)ALERTS_MAX_BYTES) {
    f.close();
    http.end();
    why = String("alert data too large (") + expected / 1024 + " KB)";
    return false;
  }
  const bool chunked = http.header("Transfer-Encoding").equalsIgnoreCase("chunked");
  String swhy;
  const long got = streamBody(http, chunked, f, ALERTS_MAX_BYTES, swhy);
  f.close();  // flushes the last buffered KB to flash; size() before this reads short
  http.end();
  tls.stop();
  size_t total = 0;
  if (File chk = SPIFFS.open(ALERTS_TMP, FILE_READ)) {
    total = chk.size();
    chk.close();
  }
  if (got < 0) {
    why = "download failed: " + swhy + " after " + String(total / 1024) + " KB";
    return false;
  }
  if (expected > 0 && total < (size_t)expected) {
    why = String("download cut off (") + total / 1024 + " of " + expected / 1024 + " KB)";
    return false;
  }
  if (total == 0) {
    why = "empty response from NWS";
    return false;
  }
  Serial.printf("[wx] NWS alerts: %u bytes saved (%s)\n", (unsigned)total, chunked ? "chunked" : "plain");
  return true;
}

// Step 2: parse the saved file with no network connection open.
bool parseAlerts(std::vector<Alert> &out, String &why) {
  File f = SPIFFS.open(ALERTS_TMP, FILE_READ);
  if (!f) {
    why = "saved alert data missing";
    return false;
  }
  // The full texts are kept only when RAM allows; otherwise the summary fields
  // are enough and the details page points to weather.gov.
  const bool fullText = ESP.getMaxAllocHeap() >= FULLTEXT_MIN_BLOCK;
  JsonDocument filter;
  for (const char *k : {"id", "event", "headline", "severity", "urgency", "certainty", "areaDesc", "onset",
                        "expires", "ends", "senderName", "status"})
    filter["features"][0]["properties"][k] = true;
  if (fullText) {
    filter["features"][0]["properties"]["description"] = true;
    filter["features"][0]["properties"]["instruction"] = true;
  }
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, f, DeserializationOption::Filter(filter));
  f.close();
  SPIFFS.remove(ALERTS_TMP);
  if (e) {
    why = String("bad alert data: ") + e.c_str();
    return false;
  }
  for (JsonObject ft : doc["features"].as<JsonArray>()) {
    JsonObject p = ft["properties"];
    if (strcmp(p["status"] | "Actual", "Actual") != 0) continue;  // skip tests and exercises
    Alert a;
    a.id = p["id"] | "";
    if (a.id.isEmpty()) continue;
    a.event = p["event"] | "Weather alert";
    a.headline = clip(p["headline"] | "", 200);
    a.severity = p["severity"] | "Unknown";
    a.urgency = p["urgency"] | "";
    a.certainty = p["certainty"] | "";
    a.area = clip(p["areaDesc"] | "", 200);
    a.description = clip(p["description"] | "", MAX_DESC);
    a.instruction = clip(p["instruction"] | "", MAX_INSTR);
    a.sender = p["senderName"] | "";
    a.onset = parseIso(p["onset"] | "");
    const char *ends = p["ends"] | "";
    a.until = parseIso(*ends ? ends : (p["expires"] | ""));
    out.push_back(a);
    if (out.size() >= MAX_ALERTS) break;
  }
  return true;
}

bool fetchAlerts(float la, float lo, std::vector<Alert> &out, String &why) {
  if (!tlsFits(why)) return false;
  if (!fsReady()) {
    why = "flash storage unavailable";
    return false;
  }
  if (!downloadAlerts(la, lo, why)) {
    SPIFFS.remove(ALERTS_TMP);
    return false;
  }
  return parseAlerts(out, why);
}

// ---------- NDBC buoy data ----------
// https://www.ndbc.noaa.gov/data/realtime2/<station>.txt holds 45 days of
// observations, newest first, as fixed columns:
//   #YY  MM DD hh mm WDIR WSPD GST  WVHT   DPD   APD MWD   PRES  ATMP  WTMP  DEWP  VIS PTDY  TIDE
//   #yr  mo dy hr mn degT m/s  m/s     m   sec   sec degT   hPa  degC  degC  degC  nmi  hPa    ft
//   (rows of numbers follow, newest first)
// Only the first few KB are read; "MM" means missing. Waves come in about
// once an hour while wind comes every 10 minutes, so each value is taken from
// the newest row that has it.

uint32_t epochUtc(int Y, int M, int D, int h, int m) {
  Y -= M <= 2;
  const int era = (Y >= 0 ? Y : Y - 399) / 400;
  const unsigned yoe = (unsigned)(Y - era * 400);
  const unsigned doy = (153 * (M + (M > 2 ? -3 : 9)) + 2) / 5 + D - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long days = era * 146097L + (long)doe - 719468L;
  return (uint32_t)(days * 86400L + h * 3600L + m * 60L);
}

// Incremental parser: fed one line at a time straight off the connection, so
// only a 200-byte line buffer is needed even while TLS holds most of the heap.
struct BuoyParse {
  enum { YY, MO, DY, HH, MN, WDIR, WSPD, GST, WVHT, DPD, APD, MWD, PRES, ATMP, WTMP, NF };
  int col[NF];
  int ncols = 0;
  float val[NF];
  uint32_t obsTime = 0, waveTime = 0;
  int rows = 0;
  bool badFormat = false;
  BuoyParse() {
    for (int &c : col) c = -1;
    for (float &v : val) v = NAN;
  }
  bool complete() const {
    for (int f = WDIR; f < NF; f++)
      if (col[f] >= 0 && std::isnan(val[f])) return false;
    return rows > 0;
  }
  // Returns true when no more lines are needed
  bool feed(char *line) {
    static const char *names[NF] = {"YY",   "MM",  "DD",  "hh",  "mm",   "WDIR", "WSPD", "GST",
                                    "WVHT", "DPD", "APD", "MWD", "PRES", "ATMP", "WTMP"};
    char *s2 = nullptr;
    if (line[0] == '#') {
      if (ncols) return false;  // the units line
      int i = 0;
      for (char *t = strtok_r(line + 1, " \r", &s2); t; t = strtok_r(nullptr, " \r", &s2), i++)
        for (int f = 0; f < NF; f++)
          if (col[f] < 0 && strcmp(t, names[f]) == 0) {
            col[f] = i;
            break;
          }
      ncols = i;
      return false;
    }
    if (!ncols || col[YY] < 0 || col[MN] < 0 || col[WVHT] < 0) {
      badFormat = true;
      return true;
    }
    char *tok[24];
    int n = 0;
    for (char *t = strtok_r(line, " \r", &s2); t && n < 24; t = strtok_r(nullptr, " \r", &s2)) tok[n++] = t;
    if (n != ncols) return false;  // not a data row (or garbled)
    uint32_t when = epochUtc(atoi(tok[col[YY]]), atoi(tok[col[MO]]), atoi(tok[col[DY]]), atoi(tok[col[HH]]),
                             atoi(tok[col[MN]]));
    if (!rows++) obsTime = when;
    for (int f = WDIR; f < NF; f++) {
      if (col[f] < 0 || !std::isnan(val[f]) || strcmp(tok[col[f]], "MM") == 0) continue;
      val[f] = atof(tok[col[f]]);
      if (f == WVHT) waveTime = when;
    }
    return complete() || rows >= 80;  // ~13 h of 10-minute rows
  }
};

// Reads lines off the body into the parser until it has what it needs or
// BUOY_READ_BYTES were read. Returns the number of bytes read.
size_t readBuoyLines(HTTPClient &http, BuoyParse &bp) {
  auto *in = http.getStreamPtr();
  const int total = http.getSize();  // -1 when chunked
  bool skipChunkHdr = total < 0;     // first line of a chunked body is its size
  char line[200];
  size_t len = 0, read = 0;
  uint32_t last = millis();
  while (in && read < BUOY_READ_BYTES && (total < 0 || read < (size_t)total) &&
         (in->connected() || in->available())) {
    int c = in->read();
    if (c < 0) {
      if (millis() - last > 10000) break;
      delay(5);
      continue;
    }
    last = millis();
    read++;
    if (c == '\n') {
      line[len] = 0;
      len = 0;
      if (skipChunkHdr) {
        skipChunkHdr = false;
        continue;
      }
      if (bp.feed(line)) break;
    } else if (len < sizeof(line) - 1) {
      line[len++] = (char)c;
    }
  }
  return read;
}

bool fetchBuoy(const String &station, Buoy &out, String &why) {
  const String path = "www.ndbc.noaa.gov/data/realtime2/" + station + ".txt";
  BuoyParse bp;
  bool got = false;
  for (int secure = 0; secure < 2 && !got; secure++) {  // plain HTTP first: no TLS, far less RAM
    std::unique_ptr<TlsGuard> guard;                     // declared first, released last
    if (secure) {
      if (!tlsFits(why)) return false;
      guard.reset(new TlsGuard());
    }
    WiFiClient plain;
    WiFiClientSecure tls;
    HTTPClient http;  // after the clients, so it is destroyed first
    http.setTimeout(15000);
    http.setConnectTimeout(15000);
    http.setReuse(false);
    bool ok;
    if (secure) {
      tls.setInsecure();  // public, read-only data
      ok = http.begin(tls, "https://" + path);
    } else {
      ok = http.begin(plain, "http://" + path);
    }
    if (!ok) {
      why = "bad URL";
      return false;
    }
    http.setUserAgent(PRODUCT_UA " (ESP32 touch display; personal weather widget)");
    int code = http.GET();
    Serial.printf("[buoy] %s %s -> %d (free %lu, largest block %lu)\n", secure ? "https" : "http", station.c_str(),
                  code, (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap());
    if (code == 200) {
      size_t n = readBuoyLines(http, bp);
      Serial.printf("[buoy] read %u bytes, %d rows\n", (unsigned)n, bp.rows);
      got = true;
    } else if (code == 404) {
      why = "station " + station + " not found (no recent data)";
      http.end();
      return false;
    } else {
      why = code < 0 ? http.errorToString(code) : String("HTTP ") + code;
      // 301/302 to https, or plain HTTP blocked: try again over TLS
    }
    http.end();
  }
  if (!got) return false;
  if (bp.badFormat) {
    why = "unexpected data format";
    return false;
  }
  if (!bp.rows) {
    why = "no observations in the data";
    return false;
  }
  out.station = station;
  out.obsTime = bp.obsTime;
  out.waveTime = bp.waveTime;
  const float *v = bp.val;
  out.waveM = v[BuoyParse::WVHT];
  out.domPeriodS = v[BuoyParse::DPD];
  out.avgPeriodS = v[BuoyParse::APD];
  out.waveDirDeg = std::isnan(v[BuoyParse::MWD]) ? -1 : (int)v[BuoyParse::MWD];
  out.windDirDeg = std::isnan(v[BuoyParse::WDIR]) ? -1 : (int)v[BuoyParse::WDIR];
  out.windMs = v[BuoyParse::WSPD];
  out.gustMs = v[BuoyParse::GST];
  out.pressHpa = v[BuoyParse::PRES];
  out.airC = v[BuoyParse::ATMP];
  out.waterC = v[BuoyParse::WTMP];
  out.fetchedMs = millis();
  out.valid = true;
  return true;
}

// ---------- Stock quote (Yahoo Finance) ----------
// https://query1.finance.yahoo.com/v8/finance/chart/<SYM>?range=1d&interval=1d
// returns chart.result[0].meta with regularMarketPrice and the previous close.
// Unofficial and free (no key), covers NYSE/NASDAQ stocks, ETFs and indices
// (^GSPC). HTTPS only, so it goes through the same flash-then-parse path as
// the NWS alerts.

// US Eastern time: EDT (UTC-4) from the 2nd Sunday of March 02:00 to the 1st
// Sunday of November 02:00, otherwise EST (UTC-5)
time_t easternNow() {
  time_t now = time(nullptr);
  struct tm u;
  gmtime_r(&now, &u);
  int y = u.tm_year + 1900;
  auto sunday = [&](int month, int n) {  // day of month of the n-th Sunday
    uint32_t first = epochUtc(y, month, 1, 0, 0);
    int dow = (int)((first / 86400 + 4) % 7);  // 1970-01-01 was a Thursday (4)
    return 1 + (7 - dow) % 7 + 7 * (n - 1);
  };
  uint32_t dstStart = epochUtc(y, 3, sunday(3, 2), 7, 0);  // 02:00 EST
  uint32_t dstEnd = epochUtc(y, 11, sunday(11, 1), 6, 0);  // 02:00 EDT
  bool dst = (uint32_t)now >= dstStart && (uint32_t)now < dstEnd;
  return now - (dst ? 4 : 5) * 3600;
}

// Weekday 09:00-16:30 Eastern (a little around the 09:30-16:00 session;
// holidays aren't known, they just get 5-minute checks too)
bool usMarketHours() {
  if (time(nullptr) < 1700000000) return true;  // clock not set yet
  time_t et = easternNow();
  struct tm t;
  gmtime_r(&et, &t);
  int mins = t.tm_hour * 60 + t.tm_min;
  return t.tm_wday >= 1 && t.tm_wday <= 5 && mins >= 9 * 60 && mins <= 16 * 60 + 30;
}

String urlEncodeSymbol(const String &s) {
  String out;
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '.' || c == '-') out += c;
    else {
      char b[4];
      snprintf(b, sizeof(b), "%%%02X", (unsigned char)c);
      out += b;
    }
  }
  return out;
}

String stockFile(size_t i) { return "/stk" + String((unsigned)i) + ".json"; }

void saveStockSyms() {  // caller holds mtx
  String v;
  for (const auto &x : stockSyms) v += x + "\n";
  prefs.begin("wx", false);
  prefs.putString("stkSyms", v);
  prefs.end();
}

// Fetches every ticker over ONE TLS session: HTTPClient keeps the
// connection open between requests to the same host (keep-alive), so the
// expensive handshake happens once. Each reply goes to its own flash file.
// Returns false only if nothing could be downloaded.
bool downloadStocks(const std::vector<String> &syms, std::vector<String> &errs, String &why) {
  TlsGuard guard;  // declared first, released after the TLS session is gone
  WiFiClientSecure tls;
  HTTPClient http;
  tls.setInsecure();  // public, read-only data
  http.setTimeout(15000);
  http.setConnectTimeout(15000);
  http.setReuse(true);
  const char *hdrs[] = {"Transfer-Encoding"};
  size_t okCount = 0;
  for (size_t i = 0; i < syms.size(); i++) {
    SPIFFS.remove(stockFile(i));
    String url = "https://query1.finance.yahoo.com/v8/finance/chart/" + urlEncodeSymbol(syms[i]) +
                 "?range=1d&interval=1d";
    if (!http.begin(tls, url)) {
      errs[i] = "bad URL";
      continue;
    }
    http.setUserAgent("Mozilla/5.0 (compatible; " PRODUCT_UA ")");
    http.collectHeaders(hdrs, 1);
    int code = http.GET();
    Serial.printf("[stock] %s -> %d (free %lu, largest block %lu)\n", syms[i].c_str(), code,
                  (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap());
    if (code != 200 && code != 404) {  // 404 still carries Yahoo's JSON error
      errs[i] = code < 0 ? http.errorToString(code) : String("HTTP ") + code;
      http.end();
      if (code < 0 && okCount == 0 && i == 0) {  // couldn't even connect: give up for now
        why = errs[i];
        return false;
      }
      continue;
    }
    File f = SPIFFS.open(stockFile(i), FILE_WRITE);
    if (!f) {
      errs[i] = "can't write to flash";
      http.end();
      continue;
    }
    const bool chunked = http.header("Transfer-Encoding").equalsIgnoreCase("chunked");
    String swhy;
    long got = streamBody(http, chunked, f, 32 * 1024, swhy);
    f.close();
    http.end();  // keeps the connection open for the next ticker when possible
    if (got <= 0) {
      errs[i] = got < 0 ? "download failed: " + swhy : String("empty response");
      SPIFFS.remove(stockFile(i));
      continue;
    }
    if (code == 404) errs[i] = "not found";  // the parser shows Yahoo's reason if any
    okCount++;
  }
  http.setReuse(false);
  http.end();
  tls.stop();
  if (!okCount) {
    why = errs.empty() ? String("no tickers") : errs[0];
    return false;
  }
  return true;
}

bool parseStockFile(size_t i, const String &sym, Stock &out, String &why) {
  File f = SPIFFS.open(stockFile(i), FILE_READ);
  if (!f) {
    why = "no data";
    return false;
  }
  JsonDocument filter;
  JsonObject fm = filter["chart"]["result"][0]["meta"].to<JsonObject>();
  for (const char *k : {"symbol", "currency", "regularMarketPrice", "chartPreviousClose", "previousClose",
                        "regularMarketDayHigh", "regularMarketDayLow", "regularMarketTime", "shortName",
                        "longName", "fullExchangeName"})
    fm[k] = true;
  filter["chart"]["error"]["description"] = true;
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, f, DeserializationOption::Filter(filter));
  f.close();
  SPIFFS.remove(stockFile(i));
  if (e) {
    why = String("bad quote data: ") + e.c_str();
    return false;
  }
  const char *err = doc["chart"]["error"]["description"] | "";
  JsonObject m = doc["chart"]["result"][0]["meta"];
  if (m.isNull() || !m["regularMarketPrice"].is<float>()) {
    why = *err ? String("not found: ") + err : String("ticker not found");
    return false;
  }
  out.symbol = m["symbol"] | sym.c_str();
  const char *nm = m["longName"] | "";
  if (!*nm) nm = m["shortName"] | "";
  out.name = nm;
  out.exchange = m["fullExchangeName"] | "";
  out.currency = m["currency"] | "USD";
  out.price = m["regularMarketPrice"].as<float>();
  float pc = m["previousClose"] | 0.0f;
  if (pc <= 0) pc = m["chartPreviousClose"] | 0.0f;
  out.prevClose = pc > 0 ? pc : NAN;
  out.dayHigh = m["regularMarketDayHigh"] | NAN;
  out.dayLow = m["regularMarketDayLow"] | NAN;
  out.marketTime = m["regularMarketTime"] | 0;
  out.fetchedMs = millis();
  out.valid = true;
  out.error = "";
  return true;
}

// ---------- NFL scores (ESPN) ----------
// http://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard lists
// this week's games with a lot of detail per game (hundreds of KB). It is
// parsed straight off the connection through BodyStream with a filter that
// keeps only teams, scores and the game clock, so it never has to fit in RAM
// or flash. Plain HTTP is tried first (no TLS); HTTPS only if redirected.

// The HTTP body as a Stream ArduinoJson can read: decodes chunked transfer
// encoding, stops at Content-Length, and waits (up to 15 s) for slow data.
class BodyStream : public Stream {
 public:
  BodyStream(Client *c, bool chunked, int length) : c_(c), chunked_(chunked), left_(length) {
    if (!chunked_ && length < 0) left_ = -1;  // unknown length: read until close
    setTimeout(200);
  }
  int available() override { return done_ ? 0 : (peeked_ >= 0 ? 1 : c_->available()); }
  int peek() override {
    if (peeked_ < 0) peeked_ = next();
    return peeked_;
  }
  int read() override {
    if (peeked_ >= 0) {
      int v = peeked_;
      peeked_ = -1;
      return v;
    }
    return next();
  }
  size_t write(uint8_t) override { return 0; }
  size_t bytes() const { return total_; }
  bool failed() const { return failed_; }

 private:
  int raw() {
    uint32_t t0 = millis();
    while (true) {
      int v = c_->read();
      if (v >= 0) return v;
      if (!c_->connected() && !c_->available()) return -1;
      if (millis() - t0 > 15000) {
        failed_ = true;
        return -1;
      }
      delay(1);
    }
  }
  int next() {
    if (done_) return -1;
    if (chunked_) {
      if (left_ <= 0) {
        if (started_) {  // CRLF after the previous chunk
          raw();
          raw();
        }
        started_ = true;
        char line[20];
        size_t k = 0;
        int v;
        while ((v = raw()) >= 0 && v != '\n')
          if (v != '\r' && k < sizeof(line) - 1) line[k++] = (char)v;
        line[k] = 0;
        if (v < 0) return finish();
        left_ = (long)strtoul(line, nullptr, 16);
        if (left_ == 0) return finish();
      }
    } else if (left_ == 0) {
      return finish();
    }
    int v = raw();
    if (v < 0) return finish();
    if (left_ > 0) left_--;
    total_++;
    return v;
  }
  int finish() {
    done_ = true;
    return -1;
  }
  Client *c_;
  bool chunked_;
  long left_;
  bool started_ = false, done_ = false, failed_ = false;
  int peeked_ = -1;
  size_t total_ = 0;
};

// "2026-11-23T18:00Z" (ESPN leaves out the seconds) -> epoch
uint32_t parseEspnDate(const char *s) {
  int Y, M, D, h, m;
  if (!s || sscanf(s, "%d-%d-%dT%d:%d", &Y, &M, &D, &h, &m) != 5) return 0;
  return epochUtc(Y, M, D, h, m);
}

bool parseScoreboard(Stream &in, std::vector<Game> &out, String &why) {
  JsonDocument filter;
  JsonObject fe = filter["events"][0].to<JsonObject>();
  fe["date"] = true;
  fe["shortName"] = true;
  fe["status"]["type"]["state"] = true;
  fe["status"]["type"]["shortDetail"] = true;
  JsonObject fc = fe["competitions"][0].to<JsonObject>();
  fc["situation"]["shortDownDistanceText"] = true;
  fc["situation"]["possession"] = true;
  JsonObject fp = fc["competitors"][0].to<JsonObject>();
  fp["id"] = true;
  fp["homeAway"] = true;
  fp["score"] = true;
  fp["team"]["abbreviation"] = true;
  JsonDocument doc;
  // ArduinoJson stops at 10 nesting levels by default, counting the parts the
  // filter skips too, and ESPN nests player leaders/links deeper than that
  // ("TooDeep"). 32 levels is plenty and costs little stack while skipping.
  DeserializationError e = deserializeJson(doc, in, DeserializationOption::Filter(filter),
                                           DeserializationOption::NestingLimit(32));
  if (e) {
    why = String("bad scoreboard data: ") + e.c_str();
    return false;
  }
  for (JsonObject ev : doc["events"].as<JsonArray>()) {
    if (out.size() >= MAX_GAMES) break;
    Game g;
    g.shortName = ev["shortName"] | "";
    g.kickoff = parseEspnDate(ev["date"] | "");
    g.state = ev["status"]["type"]["state"] | "";
    g.detail = ev["status"]["type"]["shortDetail"] | "";
    JsonObject comp = ev["competitions"][0];
    g.downDist = comp["situation"]["shortDownDistanceText"] | "";
    const char *poss = comp["situation"]["possession"] | "";
    for (JsonObject c : comp["competitors"].as<JsonArray>()) {
      bool home = strcmp(c["homeAway"] | "", "home") == 0;
      const char *abbr = c["team"]["abbreviation"] | "?";
      int score = atoi(c["score"] | "0");
      if (home) {
        g.home = abbr;
        g.homeScore = score;
      } else {
        g.away = abbr;
        g.awayScore = score;
      }
      if (*poss && strcmp(poss, c["id"] | "") == 0) g.possession = abbr;
    }
    out.push_back(g);
  }
  return true;
}

bool fetchNfl(std::vector<Game> &out, String &why) {
  const char *path = "site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard";
  for (int secure = 0; secure < 2; secure++) {  // plain HTTP first: no TLS
    std::unique_ptr<TlsGuard> guard;
    if (secure) {
      if (!tlsFits(why)) return false;
      guard.reset(new TlsGuard());
    }
    WiFiClient plain;
    WiFiClientSecure tls;
    HTTPClient http;  // after the clients, so it is destroyed first
    http.setTimeout(15000);
    http.setConnectTimeout(15000);
    http.setReuse(false);
    bool ok;
    if (secure) {
      tls.setInsecure();  // public, read-only data
      ok = http.begin(tls, String("https://") + path);
    } else {
      ok = http.begin(plain, String("http://") + path);
    }
    if (!ok) {
      why = "bad URL";
      return false;
    }
    http.setUserAgent("Mozilla/5.0 (compatible; " PRODUCT_UA ")");
    const char *hdrs[] = {"Transfer-Encoding"};
    http.collectHeaders(hdrs, 1);
    int code = http.GET();
    Serial.printf("[nfl] %s -> %d (free %lu, largest block %lu)\n", secure ? "https" : "http", code,
                  (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap());
    if (code == 200) {
      BodyStream body(http.getStreamPtr(), http.header("Transfer-Encoding").equalsIgnoreCase("chunked"),
                      http.getSize());
      bool parsed = parseScoreboard(body, out, why);
      Serial.printf("[nfl] read %u bytes, %u games\n", (unsigned)body.bytes(), (unsigned)out.size());
      http.end();
      if (!parsed && body.failed()) why = "connection stalled";
      return parsed;
    }
    why = code < 0 ? http.errorToString(code) : String("HTTP ") + code;
    http.end();
    // a redirect to HTTPS (or plain HTTP refused): try again over TLS
  }
  return false;
}

// ---------- Earthquakes (USGS) ----------
// https://earthquake.usgs.gov/fdsnws/event/1/query?format=text&starttime=...
// returns one line per event, newest first:
//   #EventID|Time|Latitude|Longitude|Depth/km|Author|Catalog|Contributor|ContributorID|MagType|Magnitude|MagAuthor|EventLocationName
//   us7000abcd|2026-11-24T18:55:12.340|35.61|-117.71|8.2|ci|ci|ci|ci40912345|ml|4.6|ci|12 km SW of Ridgecrest, CA
// Columns are found by name from the header. The body is read line by line
// straight off the connection (BodyStream decodes chunked replies), so even
// a few thousand events need only a 256-byte line buffer. 204 = no events.

// Splits on '|' keeping empty fields (strtok would merge them)
int splitBars(char *line, char **out, int maxOut) {
  int n = 0;
  char *p = line;
  while (n < maxOut) {
    out[n++] = p;
    char *bar = strchr(p, '|');
    if (!bar) break;
    *bar = 0;
    p = bar + 1;
  }
  return n;
}

void copyField(char *dst, size_t n, const char *src) {
  strncpy(dst, src ? src : "", n - 1);
  dst[n - 1] = 0;
  char *r = strchr(dst, '\r');
  if (r) *r = 0;
}

struct QuakeParse {
  enum { ID, TIME, LAT, LON, DEPTH, CONTRIB, MAGTYPE, MAG, PLACE, NF };
  int col[NF];
  int ncols = 0;
  uint32_t now = 0, midnight = 0;
  size_t keep = 0;
  BigVec<Quake> *list = nullptr;
  QuakeSummary sum;
  int rows = 0;
  bool badFormat = false;
  QuakeParse() {
    for (int &c : col) c = -1;
  }
  void feed(char *line) {
    static const char *names[NF] = {"EventID",     "Time",    "Latitude",  "Longitude",        "Depth/km",
                                    "Contributor", "MagType", "Magnitude", "EventLocationName"};
    char *tok[20];
    if (line[0] == '#') {
      int n = splitBars(line + 1, tok, 20);
      for (int i = 0; i < n; i++) {
        char *r = strchr(tok[i], '\r');
        if (r) *r = 0;
        for (int f = 0; f < NF; f++)
          if (col[f] < 0 && strcmp(tok[i], names[f]) == 0) col[f] = i;
      }
      ncols = n;
      return;
    }
    if (!line[0] || line[0] == '\r') return;
    if (!ncols || col[TIME] < 0 || col[MAG] < 0) {
      badFormat = true;
      return;
    }
    int n = splitBars(line, tok, 20);
    if (n < ncols) return;  // garbled line
    int Y, M, D, h, m, sec = 0;
    if (sscanf(tok[col[TIME]], "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec) < 5) return;
    Quake q;
    q.time = epochUtc(Y, M, D, h, m) + sec;
    q.mag = atof(tok[col[MAG]]);
    if (col[LAT] >= 0) q.lat = atof(tok[col[LAT]]);
    if (col[LON] >= 0) q.lon = atof(tok[col[LON]]);
    if (col[DEPTH] >= 0 && tok[col[DEPTH]][0]) q.depthKm = atof(tok[col[DEPTH]]);
    if (col[ID] >= 0) copyField(q.id, sizeof(q.id), tok[col[ID]]);
    if (col[MAGTYPE] >= 0) copyField(q.magType, sizeof(q.magType), tok[col[MAGTYPE]]);
    if (col[CONTRIB] >= 0) copyField(q.net, sizeof(q.net), tok[col[CONTRIB]]);
    if (col[PLACE] >= 0) copyField(q.place, sizeof(q.place), tok[col[PLACE]]);
    rows++;
    long age = (long)now - (long)q.time;
    if (q.time >= midnight) sum.today++;
    if (age <= 24L * 3600) {
      sum.h24++;
      if (!sum.haveBiggest || q.mag > sum.biggest24.mag) {
        sum.biggest24 = q;
        sum.haveBiggest = true;
      }
    }
    if (!sum.haveLatest || q.time > sum.latest.time) {
      sum.latest = q;
      sum.haveLatest = true;
    }
    if (list->size() < keep) list->push_back(q);  // rows come newest first
  }
};

// One GET to the USGS event service. Plain HTTP the first time; once USGS
// redirects to HTTPS, every later request goes straight there. onBody gets
// the body stream on 200. Returns the HTTP code (204 = no events).
template <typename F>
int streamGet(const String &hostPath, bool &useTls, F onBody, String &why) {
  for (int secure = useTls ? 1 : 0; secure < 2; secure++) {
    std::unique_ptr<TlsGuard> guard;
    if (secure) {
      if (!tlsFits(why)) return -1;
      guard.reset(new TlsGuard());
    }
    WiFiClient plain;
    WiFiClientSecure tls;
    HTTPClient http;  // after the clients, so it is destroyed first
    http.setTimeout(15000);
    http.setConnectTimeout(15000);
    http.setReuse(false);
    bool ok;
    if (secure) {
      tls.setInsecure();  // public, read-only data
      ok = http.begin(tls, "https://" + hostPath);
    } else {
      ok = http.begin(plain, "http://" + hostPath);
    }
    if (!ok) {
      why = "bad URL";
      return -1;
    }
    http.setUserAgent(PRODUCT_UA " (ESP32 touch display; personal hazard widget)");
    const char *hdrs[] = {"Transfer-Encoding"};
    http.collectHeaders(hdrs, 1);
    int code = http.GET();
    if (code == 200) {
      why = "";  // forget a redirect from the plain-HTTP attempt
      BodyStream body(http.getStreamPtr(), http.header("Transfer-Encoding").equalsIgnoreCase("chunked"),
                      http.getSize());
      onBody(body);
      if (body.failed()) why = "connection stalled";
      http.end();
      return 200;
    }
    http.end();
    if (code == 204 || code == 400 || code == 404) {
      if (code != 204) why = String("HTTP ") + code;
      return code;
    }
    why = code < 0 ? http.errorToString(code) : String("HTTP ") + code;
    if (!secure && (code == 301 || code == 302 || code == 307 || code == 308)) useTls = true;
    // a redirect to HTTPS (or plain HTTP refused): try again over TLS
  }
  return -1;
}

template <typename F>
int usgsGet(const String &pathQuery, F onBody, String &why) {
  return streamGet(String(QUAKE_HOST) + pathQuery, quakeTls, onBody, why);
}

String usgsTime(time_t t) {
  struct tm g;
  gmtime_r(&t, &g);
  char b[24];
  snprintf(b, sizeof(b), "%04d-%02d-%02dT%02d:%02d:%02d", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday, g.tm_hour,
           g.tm_min, g.tm_sec);
  return b;
}

// The magnitude/area part of every query
String usgsFilter(float minMag, int radiusKm, float la, float lo) {
  char b[96];
  int n = snprintf(b, sizeof(b), "&minmagnitude=%.1f", minMag);
  if (radiusKm > 0) snprintf(b + n, sizeof(b) - n, "&latitude=%.3f&longitude=%.3f&maxradiuskm=%d", la, lo, radiusKm);
  return b;
}

// Number of quakes in [from, to) (to = 0: up to now); -1 on failure
long usgsCount(time_t from, time_t to, const String &filter, String &why) {
  String q = "count?starttime=" + usgsTime(from) + (to ? "&endtime=" + usgsTime(to) : String("")) + filter;
  long n = -1;
  int code = usgsGet(
      q,
      [&](Stream &body) {
        // A plain number ("245"), or {"count":245,...} with format=geojson: take the first digits
        char b[64];
        size_t k = 0;
        int c;
        while (k < sizeof(b) - 1 && (c = body.read()) >= 0) b[k++] = (char)c;
        b[k] = 0;
        for (char *p = b; *p; p++)
          if (isdigit((unsigned char)*p)) {
            n = strtol(p, nullptr, 10);
            break;
          }
      },
      why);
  if (code == 204) return 0;
  if (code == 200 && n < 0) why = "unexpected count reply";
  return code == 200 ? n : -1;
}

bool fetchQuakes(float minMag, int radiusKm, float la, float lo, BigVec<Quake> &list, QuakeSummary &sum,
                 String &why) {
  time_t now = time(nullptr);
  if (now < 1700000000) {
    why = "waiting for the clock to sync";
    return false;
  }
  // "Today" starts at local midnight (the time zone is set system-wide)
  struct tm lt;
  localtime_r(&now, &lt);
  lt.tm_hour = lt.tm_min = lt.tm_sec = 0;
  time_t midnight = mktime(&lt);

  list.clear();
  list.reserve(QUAKE_KEEP);  // before TLS, while the heap is still roomy
  QuakeParse qp;
  qp.now = (uint32_t)now;
  qp.midnight = (uint32_t)midnight;
  qp.keep = QUAKE_KEEP;
  qp.list = &list;
  String q = "query?format=text&orderby=time&limit=20000&starttime=" + usgsTime(now - QUAKE_WINDOW_S) +
             usgsFilter(minMag, radiusKm, la, lo);
  int code = usgsGet(
      q,
      [&](Stream &body) {
        static char line[256];  // weather task only
        size_t len = 0;
        int c;
        while ((c = body.read()) >= 0) {
          if (c == '\n') {
            line[len] = 0;
            len = 0;
            qp.feed(line);
            if (qp.badFormat) break;
          } else if (len < sizeof(line) - 1) {
            line[len++] = (char)c;
          }
        }
        if (len) {  // last line without a newline
          line[len] = 0;
          qp.feed(line);
        }
      },
      why);
  Serial.printf("[quake] list -> %d, %d events (free %lu)\n", code, qp.rows, (unsigned long)ESP.getFreeHeap());
  if (code == 200 && qp.badFormat) {
    why = "unexpected data format";
    return false;
  }
  if (code != 200 && code != 204) return false;
  if (code == 200 && !qp.rows && why.length()) return false;  // stalled before any data
  why = "";
  qp.sum.valid = true;
  qp.sum.stored = (int)list.size();
  sum = qp.sum;
  return true;
}

// ---------- Volcanoes ----------
// U.S.: getElevatedVolcanoes returns a JSON array, one element per elevated
// volcano: volcano_name, vnum, sent_unixtime, alert_level, color_code,
// obs_fullname, notice_url... getCapElevated adds a one-line synopsis for the
// ones that have a current CAP alert; it is merged in by volcano number.

int colorRankOf(const char *c) {
  if (!strcasecmp(c, "RED")) return 4;
  if (!strcasecmp(c, "ORANGE")) return 3;
  if (!strcasecmp(c, "YELLOW")) return 2;
  if (!strcasecmp(c, "GREEN")) return 1;
  return 0;
}
int alertRankOf(const char *a) {
  if (!strcasecmp(a, "WARNING")) return 4;
  if (!strcasecmp(a, "WATCH")) return 3;
  if (!strcasecmp(a, "ADVISORY")) return 2;
  if (!strcasecmp(a, "NORMAL")) return 1;
  return 0;
}

// Drops anything outside printable ASCII (the built-in fonts have nothing else)
void asciiCopy(char *dst, size_t n, const char *src) {
  size_t k = 0;
  for (const char *p = src ? src : ""; *p && k < n - 1; p++) {
    unsigned char c = (unsigned char)*p;
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    if (c >= 32 && c < 127) dst[k++] = (char)c;
  }
  dst[k] = 0;
}

bool fetchUsVolcanoes(std::vector<Volcano> &out, String &why) {
  JsonDocument filter;
  for (const char *k : {"volcano_name", "vnum", "sent_unixtime", "alert_level", "color_code", "obs_fullname"})
    filter[0][k] = true;
  bool parsed = false;
  int code = streamGet(
      VOLC_ELEVATED, volcTls,
      [&](Stream &body) {
        JsonDocument doc;
        DeserializationError e = deserializeJson(doc, body, DeserializationOption::Filter(filter));
        if (e) {
          why = String("bad USGS data: ") + e.c_str();
          return;
        }
        for (JsonObject v : doc.as<JsonArray>()) {
          Volcano x;
          asciiCopy(x.name, sizeof(x.name), v["volcano_name"] | "");
          asciiCopy(x.vnum, sizeof(x.vnum), v["vnum"] | "");
          asciiCopy(x.alert, sizeof(x.alert), v["alert_level"] | "");
          asciiCopy(x.color, sizeof(x.color), v["color_code"] | "");
          asciiCopy(x.obs, sizeof(x.obs), v["obs_fullname"] | "");
          x.sent = v["sent_unixtime"].is<const char *>() ? (uint32_t)atol(v["sent_unixtime"].as<const char *>())
                                                          : v["sent_unixtime"].as<uint32_t>();
          if (!x.name[0]) continue;
          // One entry per volcano: keep its newest notice
          auto it = std::find_if(out.begin(), out.end(), [&](const Volcano &o) { return !strcmp(o.vnum, x.vnum); });
          if (it == out.end()) {
            if (out.size() < VOLC_MAX) out.push_back(x);
          } else if (x.sent > it->sent) {
            *it = x;
          }
        }
        parsed = true;
      },
      why);
  if (code == 204) return true;
  if (code != 200 || !parsed) {
    if (why.isEmpty()) why = "no reply from USGS";
    return false;
  }
  // Synopses (best effort; the list above is what matters)
  JsonDocument f2;
  for (const char *k : {"vnum", "synopsis"}) f2[0][k] = true;
  String w2;
  streamGet(
      VOLC_CAP, volcTls,
      [&](Stream &body) {
        JsonDocument doc;
        if (deserializeJson(doc, body, DeserializationOption::Filter(f2))) return;
        for (JsonObject v : doc.as<JsonArray>()) {
          const char *vn = v["vnum"] | "";
          for (auto &x : out)
            if (!strcmp(x.vnum, vn)) asciiCopy(x.synopsis, sizeof(x.synopsis), v["synopsis"] | "");
        }
      },
      w2);
  std::sort(out.begin(), out.end(), [](const Volcano &a, const Volcano &b) {
    int ra = colorRankOf(a.color) * 10 + alertRankOf(a.alert), rb = colorRankOf(b.color) * 10 + alertRankOf(b.alert);
    if (ra != rb) return ra > rb;
    return strcmp(a.name, b.name) < 0;
  });
  why = "";
  return true;
}

// Worldwide: the Smithsonian / USGS Weekly Volcanic Activity Report RSS. Each
// <item> has a <title> like "Etna (Italy) - Report for 19 November-25
// November 2025 - New Activity/Unrest", a <description> with the report text
// (often HTML-escaped), and a <georss:point>. A small tag scanner reads it
// straight off the connection; only the first ~600 characters of each
// description are kept.
struct RssScan {
  enum Field { NONE, TITLE, DESC, POINT };
  BigVec<Eruption> *out = nullptr;
  String week;
  bool inItem = false, inTag = false, inCdata = false;
  Field field = NONE;
  char tag[24];
  size_t tagLen = 0;
  char buf[640];
  size_t bufLen = 0;
  Eruption cur;
  char title[160];
  int items = 0;
  char cdEnd[3] = {0, 0, 0};

  void putText(char c) {
    if (field != NONE && bufLen < sizeof(buf) - 1) buf[bufLen++] = c;
  }
  void beginField(Field f) {
    field = f;
    bufLen = 0;
  }
  void endField() {
    buf[bufLen] = 0;
    if (field == TITLE) strncpy(title, buf, sizeof(title) - 1), title[sizeof(title) - 1] = 0;
    else if (field == DESC) cleanText(buf, cur.summary, sizeof(cur.summary));
    else if (field == POINT) sscanf(buf, "%f %f", &cur.lat, &cur.lon);
    field = NONE;
  }
  // Decodes entities, strips HTML tags and squeezes whitespace
  static void cleanText(char *s, char *dst, size_t n) {
    // entities -> characters (in place; the result is never longer). Twice,
    // for feeds that escape the HTML and then the entities in it ("&amp;amp;")
    for (int pass = 0; pass < 2; pass++) {
    char *w = s;
    for (char *r = s; *r;) {
      if (*r == '&') {
        const char *ents[] = {"&lt;", "&gt;", "&amp;", "&quot;", "&#39;", "&apos;", "&nbsp;"};
        const char repl[] = {'<', '>', '&', '"', '\'', '\'', ' '};
        bool done = false;
        for (size_t i = 0; i < sizeof(repl); i++) {
          size_t L = strlen(ents[i]);
          if (!strncmp(r, ents[i], L)) {
            *w++ = repl[i];
            r += L;
            done = true;
            break;
          }
        }
        if (!done && r[1] == '#') {  // numeric: skip it (not in our fonts)
          char *semi = strchr(r, ';');
          if (semi && semi - r < 10) {
            r = semi + 1;
            continue;
          }
        }
        if (!done) *w++ = *r++;
      } else {
        *w++ = *r++;
      }
    }
    *w = 0;
    }
    size_t k = 0;
    bool inHtml = false, space = true;
    for (char *r = s; *r && k < n - 1; r++) {
      unsigned char c = (unsigned char)*r;
      if (c == '<') {
        inHtml = true;
        continue;
      }
      if (c == '>' && inHtml) {
        inHtml = false;
        c = ' ';
      } else if (inHtml) {
        continue;
      }
      if (c == '\n' || c == '\r' || c == '\t') c = ' ';
      if (c < 32 || c >= 127) continue;
      if (c == ' ') {
        if (space) continue;
        space = true;
      } else {
        space = false;
      }
      dst[k++] = (char)c;
    }
    while (k && dst[k - 1] == ' ') k--;
    dst[k] = 0;
  }
  void finishItem() {
    // "Name (Country) - Report for <dates> - <status>"
    char t[160];
    cleanText(title, t, sizeof(t));
    const char *paren = strchr(t, '(');
    const char *close = paren ? strchr(paren, ')') : nullptr;
    if (paren) {
      size_t L = paren - t;
      while (L && t[L - 1] == ' ') L--;
      size_t m = std::min(L, sizeof(cur.name) - 1);
      memcpy(cur.name, t, m);
      cur.name[m] = 0;
      if (close) {
        size_t c = std::min((size_t)(close - paren - 1), sizeof(cur.country) - 1);
        memcpy(cur.country, paren + 1, c);
        cur.country[c] = 0;
      }
    } else {
      asciiCopy(cur.name, sizeof(cur.name), t);
    }
    const char *rep = strstr(t, "Report for ");
    if (rep && week.isEmpty()) {
      String w = rep + 11;
      int dash = w.indexOf(" - ");
      if (dash > 0) w = w.substring(0, dash);
      week = w;
    }
    // "New" anywhere after the dates marks new activity this week
    const char *tail = rep ? rep : (close ? close : t);
    String ts = tail;
    ts.toLowerCase();
    cur.isNew = ts.indexOf("new") >= 0;
    if (cur.name[0] && out->size() < ERUPT_MAX) out->push_back(cur);
    if (!items++) Serial.printf("[volcano] first report item: %s\n", t);
  }
  void onTag() {
    tag[tagLen] = 0;
    char *sp = strpbrk(tag, " \t/");
    bool closing = tag[0] == '/';
    const char *name = closing ? tag + 1 : tag;
    if (sp && sp != tag) *sp = 0;  // drop attributes (and a self-closing '/')
    if (!closing && !strcmp(name, "item")) {
      inItem = true;
      cur = Eruption();
      title[0] = 0;
    } else if (closing && !strcmp(name, "item")) {
      if (inItem) finishItem();
      inItem = false;
    } else if (inItem) {
      Field f = !strcmp(name, "title") ? TITLE : !strcmp(name, "description") ? DESC
                : !strcmp(name, "georss:point") ? POINT : NONE;
      if (f != NONE) {
        if (closing) endField();
        else beginField(f);
      }
    }
  }
  void feed(char c) {
    if (inCdata) {  // copy until "]]>"
      cdEnd[0] = cdEnd[1];
      cdEnd[1] = cdEnd[2];
      cdEnd[2] = c;
      putText(c);
      if (cdEnd[0] == ']' && cdEnd[1] == ']' && cdEnd[2] == '>') {
        inCdata = false;
        if (bufLen >= 3) bufLen -= 3;
      }
      return;
    }
    if (inTag) {
      if (c == '>') {
        inTag = false;
        onTag();
        return;
      }
      if (tagLen < sizeof(tag) - 1) tag[tagLen++] = c;
      if (tagLen == 8 && !strncmp(tag, "![CDATA[", 8)) {
        inTag = false;
        inCdata = true;
        cdEnd[0] = cdEnd[1] = cdEnd[2] = 0;
      }
      return;
    }
    if (c == '<') {
      inTag = true;
      tagLen = 0;
      return;
    }
    putText(c);
  }
};

bool fetchEruptions(BigVec<Eruption> &out, String &week, String &why) {
  out.reserve(ERUPT_MAX);
  std::unique_ptr<RssScan> scan(new RssScan());  // ~1 KB of buffers: heap, not the task stack
  scan->out = &out;
  int code = streamGet(
      ERUPT_RSS, eruptTls,
      [&](Stream &body) {
        int c;
        while ((c = body.read()) >= 0) scan->feed((char)c);
      },
      why);
  Serial.printf("[volcano] weekly report -> %d, %u volcanoes\n", code, (unsigned)out.size());
  if (code != 200) {
    if (why.isEmpty()) why = "no reply from the Smithsonian";
    return false;
  }
  if (out.empty()) {
    if (why.isEmpty()) why = "report had no volcanoes (format changed?)";
    return false;
  }
  week = scan->week;
  why = "";
  return true;
}

// ---------- Airport flights (live ADS-B) ----------

#if defined(BOARD_NM_CYD_C5)
// JSON documents in PSRAM on the C5
struct PsJsonAlloc : ArduinoJson::Allocator {
  void *allocate(size_t n) override {
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
  }
  void deallocate(void *p) override { heap_caps_free(p); }
  void *reallocate(void *p, size_t n) override {
    void *q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return q ? q : realloc(p, n);
  }
};
PsJsonAlloc psJson;
#endif

// Airline callsign prefixes (ICAO) seen at U.S. airports. `mkt` is the
// two-letter code the flight is sold under, so a PSA-operated American
// Eagle flight JIA5462 reads "AA 5462".
struct AirlineCode {
  const char *icao, *mkt, *name;
};
const AirlineCode AIRLINES[] = {
    {"DAL", "DL", "Delta"},           {"AAL", "AA", "American"},       {"JBU", "B6", "JetBlue"},
    {"MXY", "MX", "Breeze"},          {"VXP", "XP", "Avelo"},          {"SCX", "SY", "Sun Country"},
    {"UAL", "UA", "United"},          {"SWA", "WN", "Southwest"},      {"NKS", "NK", "Spirit"},
    {"FFT", "F9", "Frontier"},        {"AAY", "G4", "Allegiant"},      {"ASA", "AS", "Alaska"},
    {"HAL", "HA", "Hawaiian"},        {"EDV", "DL", "Delta Connection (Endeavor)"},
    {"JIA", "AA", "American Eagle (PSA)"}, {"ENY", "AA", "American Eagle (Envoy)"},
    {"PDT", "AA", "American Eagle (Piedmont)"}, {"SKW", "OO", "SkyWest"}, {"RPA", "YX", "Republic"},
    {"ASH", "UA", "United Express (Mesa)"}, {"GJS", "UA", "United Express (GoJet)"},
    {"CPZ", "UA", "United Express (CommutAir)"}, {"QXE", "AS", "Horizon"}, {"FDX", "FX", "FedEx"},
    {"UPS", "5X", "UPS"},             {"GTI", "5Y", "Atlas Air"},      {"ABX", "GB", "ABX Air"},
    {"ATN", "8C", "ATI"},             {"CKS", "K4", "Kalitta"},        {"ACA", "AC", "Air Canada"},
    {"JZA", "QK", "Air Canada Jazz"}, {"WJA", "WS", "WestJet"},        {"TSC", "TS", "Air Transat"},
    {"POE", "PD", "Porter"},          {"SIL", "3M", "Silver Airways"}, {"VIV", "VB", "Viva"},
    {"AMX", "AM", "Aeromexico"},      {"VOI", "Y4", "Volaris"},        {"CMP", "CM", "Copa"},
    {"BAW", "BA", "British Airways"}, {"VIR", "VS", "Virgin Atlantic"}, {"DLH", "LH", "Lufthansa"},
};

// Fills in the marketed flight number and airline for an airline callsign.
// Light aircraft and business jets return false (only airline flights count).
bool airlineFor(const char *cs, uint8_t category, char *flight, size_t fn, char *airline, size_t an) {
  size_t L = strlen(cs);
  if (L < 4 || L > 8) return false;
  for (int i = 0; i < 3; i++)
    if (!isupper((unsigned char)cs[i])) return false;
  if (!isdigit((unsigned char)cs[3])) return false;  // "DAL3154"; "N123AB" fails here
  const char *num = cs + 3;
  for (const auto &a : AIRLINES)
    if (!strncmp(cs, a.icao, 3)) {
      snprintf(flight, fn, "%s %s", a.mkt, num);
      snprintf(airline, an, "%s", a.name);
      return true;
    }
  // Unknown operator: count it only if the aircraft is airliner-sized
  // (ADS-B emitter category A3 large / A4 / A5 heavy)
  if (category < 3) return false;
  snprintf(flight, fn, "%s", cs);
  snprintf(airline, an, "%.3s", cs);
  return true;
}

struct TypeName {
  const char *code, *name;
};
const TypeName TYPES[] = {
    {"E75L", "Embraer 175"},  {"E75S", "Embraer 175"},   {"E170", "Embraer 170"},  {"E190", "Embraer 190"},
    {"E195", "Embraer 195"},  {"E290", "Embraer E190-E2"}, {"CRJ2", "Bombardier CRJ200"}, {"CRJ7", "Bombardier CRJ700"},
    {"CRJ9", "Bombardier CRJ900"}, {"BCS1", "Airbus A220-100"}, {"BCS3", "Airbus A220-300"}, {"A319", "Airbus A319"},
    {"A320", "Airbus A320"},  {"A20N", "Airbus A320neo"}, {"A321", "Airbus A321"},  {"A21N", "Airbus A321neo"},
    {"B712", "Boeing 717"},   {"B737", "Boeing 737-700"}, {"B738", "Boeing 737-800"}, {"B739", "Boeing 737-900"},
    {"B38M", "Boeing 737 MAX 8"}, {"B39M", "Boeing 737 MAX 9"}, {"B752", "Boeing 757-200"}, {"B753", "Boeing 757-300"},
    {"B763", "Boeing 767-300"}, {"B772", "Boeing 777-200"}, {"B788", "Boeing 787-8"}, {"B789", "Boeing 787-9"},
    {"MD88", "McDonnell Douglas MD-88"}, {"DH8D", "Dash 8-400"}, {"AT76", "ATR 72-600"},
};

// Track of one airliner across polls (weather task only)
struct FltTrack {
  char hex[8], callsign[10], flight[10], airline[28], type[6], reg[10];
  uint32_t firstMs, lastMs, loggedMs;
  int32_t firstAgl, lastAgl, minAgl;
  float lastDist;
  int16_t lastRate, lastTrack, lastGs;
  bool everGround, lastGround, depLogged, arrLogged, seenNow;
};
std::vector<FltTrack> fltTracks;

float nmBetween(float la1, float lo1, float la2, float lo2) {
  const float d2r = (float)M_PI / 180.0f;
  float dLat = (la2 - la1) * d2r, dLon = (lo2 - lo1) * d2r;
  float a = sinf(dLat / 2) * sinf(dLat / 2) + cosf(la1 * d2r) * cosf(la2 * d2r) * sinf(dLon / 2) * sinf(dLon / 2);
  return 2 * 3440.065f * atan2f(sqrtf(a), sqrtf(1 - a));
}

uint32_t localDay(time_t t) {
  struct tm lt;
  localtime_r(&t, &lt);
  return (uint32_t)((lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday);
}

// Records a takeoff or landing (holds mtx)
void logFlight(bool dep, const FltTrack &t, uint32_t when, int16_t heading, int16_t gs, int32_t agl, float magVar) {
  FlightEvent e;
  e.time = when;
  strcpy(e.callsign, t.callsign);
  strcpy(e.flight, t.flight);
  strcpy(e.airline, t.airline);
  strcpy(e.type, t.type);
  strcpy(e.reg, t.reg);
  e.heading = heading;
  if (heading >= 0) {  // runways are numbered by magnetic heading
    int mag = (int)lroundf(heading - magVar);
    mag = ((mag % 360) + 360) % 360;
    int rw = (mag + 5) / 10;
    e.runway = rw == 0 ? 36 : (rw > 36 ? rw - 36 : rw);
  }
  e.gsKt = gs;
  e.altFt = agl;
  auto &list = dep ? fltDeps : fltArrs;
  list.insert(list.begin(), e);
  if (list.size() > FLT_KEEP) list.resize(FLT_KEEP);
  uint32_t day = localDay((time_t)when);
  if (day != fltDay) {
    fltDay = day;
    fltDepToday = fltArrToday = 0;
  }
  (dep ? fltDepToday : fltArrToday)++;
  prefs.begin("wx", false);  // counts survive a restart (same day only)
  prefs.putUInt("flDay", fltDay);
  prefs.putInt("flDep", fltDepToday);
  prefs.putInt("flArr", fltArrToday);
  prefs.end();
  fltVer++;
  Serial.printf("[flights] %s: %s (%s) %s %s, hdg %d, rwy %d, %d kt, %ld ft\n", dep ? "TAKEOFF" : "LANDING", e.flight,
                e.airline, e.type, e.reg, heading, e.runway, gs, (long)agl);
}

// Looks up the airport's position (AviationWeather.gov airport data)
bool resolveAirport(const String &code, float &la, float &lo, int &elevFt, float &magVar, String &name, String &why) {
  String ids[2];
  if (code.length() == 4) ids[0] = code;
  else {
    ids[0] = "K" + code;  // contiguous U.S.
    ids[1] = "P" + code;  // Alaska, Hawaii, Pacific (PANC, PHNL, PGUM)
  }
  for (const auto &id : ids) {
    if (id.isEmpty()) continue;
    bool got = false;
    JsonDocument filter;
    for (const char *k : {"lat", "lon", "elev", "name", "magdec"}) filter[0][k] = true;
    int c = streamGet(
        "aviationweather.gov/api/data/airport?ids=" + id + "&format=json", avwxTls,
        [&](Stream &body) {
          JsonDocument doc;
          if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return;
          JsonObject a = doc[0];
          if (a.isNull() || !a["lat"].is<float>()) return;
          la = a["lat"];
          lo = a["lon"];
          elevFt = (int)lroundf((a["elev"] | 0.0f) * 3.28084f);  // meters
          char nm[48];
          asciiCopy(nm, sizeof(nm), a["name"] | "");
          name = nm;
          const char *md = a["magdec"] | "";  // "6W" / "3E"
          magVar = atof(md);
          if (strchr(md, 'W') || strchr(md, 'w')) magVar = -magVar;
          got = true;
        },
        why);
    Serial.printf("[flights] airport lookup %s -> %d%s\n", id.c_str(), c, got ? "" : " (not found)");
    if (got) {
      why = "";
      return true;
    }
  }
  if (why.isEmpty()) why = "airport " + code + " not found";
  return false;
}

// One ADS-B poll: update the tracks and detect takeoffs/landings
bool pollFlights(String &why) {
  float la, lo, mv;
  int elev;
  bool have;
  String code, nm;
  {
    std::lock_guard<std::mutex> lock(mtx);
    la = apLat;
    lo = apLon;
    elev = apElevFt;
    mv = apMagVar;
    have = apHaveLoc;
    code = apCode;
  }
  if (!have) {
    if (!resolveAirport(code, la, lo, elev, mv, nm, why)) return false;
    std::lock_guard<std::mutex> lock(mtx);
    if (code != apCode) return true;  // changed meanwhile
    apLat = la;
    apLon = lo;
    apElevFt = elev;
    apMagVar = mv;
    apName = nm;
    apHaveLoc = true;
    apDirty = true;
    fltVer++;
  }
  time_t now = time(nullptr);
  if (now < 1700000000) {
    why = "waiting for the clock to sync";
    return false;
  }

  JsonDocument filter;
  for (const char *arr : {"ac", "aircraft"})
    for (const char *k : {"hex", "flight", "r", "t", "category", "alt_baro", "baro_rate", "geom_rate", "gs", "track",
                          "lat", "lon", "seen", "seen_pos"})
      filter[arr][0][k] = true;
#if defined(BOARD_NM_CYD_C5)
  JsonDocument doc(&psJson);
#else
  JsonDocument doc;
#endif
  char path[96];
  bool parsed = false;
  int code2 = -1;
  for (int attempt = 0; attempt < 2; attempt++) {
    snprintf(path, sizeof(path), "%sv%d/lat/%.4f/lon/%.4f/dist/%d", ADSB_HOST, adsbApi, la, lo, FLT_RADIUS_NM);
    code2 = streamGet(
        path, adsbTls,
        [&](Stream &body) {
          DeserializationError e = deserializeJson(doc, body, DeserializationOption::Filter(filter));
          if (e) why = String("bad ADS-B data: ") + e.c_str();
          else parsed = true;
        },
        why);
    if (code2 == 404 && adsbApi == 3) {  // older API path
      adsbApi = 2;
      why = "";
      continue;
    }
    break;
  }
  Serial.printf("[flights] adsb.fi v%d -> %d (%s)\n", adsbApi, code2, adsbTls ? "https" : "http");
  if (code2 != 200 || !parsed) {
    if (why.isEmpty()) why = String("adsb.fi replied ") + code2;
    return false;
  }
  JsonArray acs = doc["ac"].is<JsonArray>() ? doc["ac"].as<JsonArray>() : doc["aircraft"].as<JsonArray>();
  if (acs.isNull()) {
    why = "no aircraft list in the ADS-B reply";
    return false;
  }
  int total = 0, airliners = 0;

  uint32_t nowMs = millis();
  std::vector<LiveFlight> live;
  std::lock_guard<std::mutex> lock(mtx);  // logFlight, the tracks and the lists below need it
  if (code != apCode) return true;        // airport changed while we were downloading
  for (auto &t : fltTracks) t.seenNow = false;
  uint32_t today = localDay(now);
  if (today != fltDay) {  // midnight: start the day's counts over
    fltDay = today;
    fltDepToday = fltArrToday = 0;
    fltVer++;
  }
  for (JsonObject a : acs) {
    total++;
    char cs[10];
    asciiCopy(cs, sizeof(cs), a["flight"] | "");
    for (int i = (int)strlen(cs) - 1; i >= 0 && cs[i] == ' '; i--) cs[i] = 0;  // padded with spaces
    const char *catS = a["category"] | "";
    uint8_t cat = (catS[0] == 'A' && catS[1] >= '0' && catS[1] <= '9') ? catS[1] - '0' : 0;
    char flight[10], airline[28];
    if (!airlineFor(cs, cat, flight, sizeof(flight), airline, sizeof(airline))) continue;
    airliners++;
    if (a["lat"].isNull() || a["alt_baro"].isNull()) continue;
    float seen = a["seen_pos"] | (a["seen"] | 0.0f);
    if (seen > 30) continue;  // stale position
    bool ground = a["alt_baro"].is<const char *>() && !strcmp(a["alt_baro"].as<const char *>(), "ground");
    int32_t agl = ground ? 0 : (int32_t)(a["alt_baro"].as<float>()) - elev;
    if (agl < 0) agl = 0;
    float dist = nmBetween(la, lo, a["lat"].as<float>(), a["lon"].as<float>());
    int16_t rate = a["baro_rate"].is<int>() ? a["baro_rate"].as<int>() : (a["geom_rate"] | 0);
    int16_t trk = a["track"].isNull() ? -1 : (int16_t)a["track"].as<float>();
    int16_t gs = a["gs"].isNull() ? -1 : (int16_t)a["gs"].as<float>();
    char hex[8];
    asciiCopy(hex, sizeof(hex), a["hex"] | "");

    FltTrack *t = nullptr;
    for (auto &x : fltTracks)
      if (!strcmp(x.hex, hex)) t = &x;
    bool fresh = !t;
    if (fresh) {
      if (fltTracks.size() >= FLT_TRACKS) continue;
      FltTrack n{};
      strcpy(n.hex, hex);
      n.firstMs = nowMs;
      n.firstAgl = agl;
      n.minAgl = agl;
      n.lastGround = ground;
      fltTracks.push_back(n);
      t = &fltTracks.back();
    }
    strcpy(t->callsign, cs);
    strcpy(t->flight, flight);
    strcpy(t->airline, airline);
    asciiCopy(t->type, sizeof(t->type), a["t"] | "");
    asciiCopy(t->reg, sizeof(t->reg), a["r"] | "");
    uint32_t when = (uint32_t)now - (uint32_t)seen;
    bool recentlyLogged = t->loggedMs && nowMs - t->loggedMs < 20UL * 60 * 1000;
    bool climbing = rate >= 300 || (!fresh && !t->lastGround && agl - t->lastAgl >= 200);

    // Takeoff: airborne, low and climbing near the field, after being on
    // the ground or first seen low (receivers often can't see the runway)
    if (!t->depLogged && !recentlyLogged && !ground && dist <= 6 && agl <= 3500 && climbing &&
        (t->everGround || t->firstAgl <= 2000 || t->minAgl <= 1500)) {
      logFlight(true, *t, when, trk, gs, agl, mv);
      t->depLogged = true;
      t->loggedMs = nowMs;
    }
    // Landing: on the ground at the field after being airborne (or still
    // rolling fast when first seen)
    if (!t->arrLogged && !recentlyLogged && ground && dist <= 3 &&
        ((!fresh && !t->lastGround) || gs >= 50)) {
      logFlight(false, *t, when, t->lastTrack >= 0 ? t->lastTrack : trk, t->lastGs >= 0 ? t->lastGs : gs, 0, mv);
      t->arrLogged = true;
      t->loggedMs = nowMs;
    }
    t->lastMs = nowMs;
    t->lastAgl = agl;
    t->lastDist = dist;
    t->lastRate = rate;
    t->lastTrack = trk;
    t->lastGs = gs;
    t->lastGround = ground;
    t->everGround |= ground;
    if (agl < t->minAgl) t->minAgl = agl;
    t->seenNow = true;

    LiveFlight lf;
    strcpy(lf.callsign, cs);
    strcpy(lf.flight, flight);
    strcpy(lf.airline, airline);
    strcpy(lf.type, t->type);
    lf.distNm = dist;
    lf.altFt = agl;
    lf.gsKt = gs < 0 ? 0 : gs;
    lf.rateFpm = rate;
    // Within 10 nm and descending is an approach (jets start down from ~10,000 ft out here)
    lf.phase = ground ? 3 : rate >= 300 ? 2 : (rate <= -300 && agl < 12000) ? 1 : 0;
    live.push_back(lf);
    Serial.printf("[flights]   %s %s %s: %.1f nm, %s%ld ft, %d fpm, %d kt\n", flight, cs, t->type, dist,
                  ground ? "GROUND " : "", (long)agl, rate, gs);
  }
  // Landing out of sight: last seen low, close in and descending, then gone
  // (below the receivers' coverage on short final)
  for (auto &t : fltTracks) {
    if (t.seenNow || t.arrLogged || t.lastGround) continue;
    uint32_t gone = nowMs - t.lastMs;
    bool recentlyLogged = t.loggedMs && nowMs - t.loggedMs < 20UL * 60 * 1000;
    if (!recentlyLogged && gone >= 45000 && gone <= 10UL * 60 * 1000 && t.lastDist <= 4 && t.lastAgl <= 1200 &&
        (t.lastRate <= -200 || t.lastAgl < t.firstAgl - 300)) {
      logFlight(false, t, (uint32_t)now - gone / 1000, t.lastTrack, t.lastGs, t.lastAgl, mv);
      t.arrLogged = true;
      t.loggedMs = nowMs;
    }
  }
  // Forget aircraft not seen for 10 minutes
  fltTracks.erase(std::remove_if(fltTracks.begin(), fltTracks.end(),
                                 [&](const FltTrack &t) { return nowMs - t.lastMs > 10UL * 60 * 1000; }),
                  fltTracks.end());
  std::sort(live.begin(), live.end(), [](const LiveFlight &x, const LiveFlight &y) { return x.distNm < y.distNm; });
  Serial.printf("[flights] %s: %d aircraft within %d nm, %d airliner%s; today %d takeoffs, %d landings\n",
                code.c_str(), total, FLT_RADIUS_NM, airliners, airliners == 1 ? "" : "s", fltDepToday, fltArrToday);
  fltLive.swap(live);
  fltNearTotal = total;
  fltVer++;
  why = "";
  return true;
}

// The tracked team's game this week, or nullptr (caller holds mtx)
const Game *teamGame() {
  if (nflTeamAbbr.isEmpty()) return nullptr;
  for (const auto &g : nflGames)
    if (g.home == nflTeamAbbr || g.away == nflTeamAbbr) return &g;
  return nullptr;
}

void saveSnoozed() {  // caller holds mtx
  String s;
  for (const auto &id : snoozedIds) s += id + "\n";
  prefs.begin("wx", false);
  prefs.putString("asnz", s);
  prefs.end();
}

// Replace the alert list with a fresh fetch (holds mtx)
void applyAlerts(std::vector<Alert> &fresh) {
  for (auto &a : fresh) {
    a.snoozed = std::find(snoozedIds.begin(), snoozedIds.end(), a.id) != snoozedIds.end();
    if (std::find(poppedIds.begin(), poppedIds.end(), a.id) == poppedIds.end()) {
      poppedIds.push_back(a.id);
      if (!a.snoozed) popupQueue.push_back(a.id);
      Serial.printf("[wx] NWS alert: %s (%s)\n", a.event.c_str(), a.severity.c_str());
    }
  }
  std::sort(fresh.begin(), fresh.end(), [](const Alert &x, const Alert &y) {
    return severityRank(x.severity) > severityRank(y.severity);
  });
  // Forget snoozes and announcements for alerts that are no longer active
  auto active = [&](const String &id) {
    for (const auto &a : fresh)
      if (a.id == id) return true;
    return false;
  };
  size_t before = snoozedIds.size();
  snoozedIds.erase(std::remove_if(snoozedIds.begin(), snoozedIds.end(), [&](const String &id) { return !active(id); }),
                   snoozedIds.end());
  if (snoozedIds.size() != before) saveSnoozed();
  poppedIds.erase(std::remove_if(poppedIds.begin(), poppedIds.end(), [&](const String &id) { return !active(id); }),
                  poppedIds.end());
  alertList.swap(fresh);
  alertsVer++;
}

// All the fetch work lives here so its locals (Strings, JSON) are destroyed
// when it returns. vTaskDelete() never returns, so anything still in scope in
// the task function itself would leak on every fetch.
void buoyWork() {
  String id;
  {
    std::lock_guard<std::mutex> lock(mtx);
    id = buoyId;
  }
  Buoy b;
  String why;
  bool ok = fetchBuoy(id, b, why);
  {
    std::lock_guard<std::mutex> lock(mtx);
    if (id == buoyId) {  // ignore if the station was changed meanwhile
      if (ok) {
        buoyData = b;
        buoyErr = "";
      } else {
        buoyErr = why;
      }
      buoyVer++;
    }
  }
  if (ok)
    Serial.printf("[buoy] %s: waves %.2f m, period %.0f s, water %.1f C\n", id.c_str(), b.waveM, b.domPeriodS,
                  b.waterC);
  else
    Serial.printf("[buoy] %s failed: %s\n", id.c_str(), why.c_str());
  byLastOk = ok;
}

void stockWork() {
  std::vector<String> syms;
  {
    std::lock_guard<std::mutex> lock(mtx);
    syms = stockSyms;
  }
  if (syms.empty()) return;
  std::vector<String> errs(syms.size());
  std::vector<Stock> fresh(syms.size());
  String why;
  bool ok = fsReady() ? true : (why = "flash storage unavailable", false);
  if (ok) ok = tlsFits(why);
  if (ok) ok = downloadStocks(syms, errs, why);  // TLS is closed when this returns
  if (ok) {
    for (size_t i = 0; i < syms.size(); i++) {
      if (!errs[i].length() || errs[i] == "not found") {
        String perr;
        if (!parseStockFile(i, syms[i], fresh[i], perr)) errs[i] = perr;
      }
      if (errs[i].length()) Serial.printf("[stock] %s failed: %s\n", syms[i].c_str(), errs[i].c_str());
      else Serial.printf("[stock] %s %.2f (prev close %.2f)\n", syms[i].c_str(), fresh[i].price, fresh[i].prevClose);
    }
  } else {
    for (size_t i = 0; i < syms.size(); i++) SPIFFS.remove(stockFile(i));
    Serial.printf("[stock] update failed: %s\n", why.c_str());
  }
  {
    std::lock_guard<std::mutex> lock(mtx);
    if (syms == stockSyms) {  // ignore if the list was changed meanwhile
      if (ok) {
        for (size_t i = 0; i < syms.size(); i++) {
          if (fresh[i].valid) stockList[i] = fresh[i];
          stockList[i].symbol = syms[i];
          stockList[i].error = errs[i];  // keeps the last good price if this one failed
        }
        stockErr = "";
        stockFetchedMs = millis();
      } else {
        stockErr = why;
      }
      stockVer++;
    }
  }
  stLastOk = ok;
}

void nflWork() {
  std::vector<Game> games;
  String why;
  bool ok = fetchNfl(games, why);
  {
    std::lock_guard<std::mutex> lock(mtx);
    if (ok) {
      nflGames.swap(games);
      nflErr = "";
      nflFetchedMs = millis();
    } else {
      nflErr = why;
    }
    nflVer++;
    if (ok) {
      const Game *g = teamGame();
      if (g)
        Serial.printf("[nfl] %s %d - %s %d, %s\n", g->away.c_str(), g->awayScore, g->home.c_str(), g->homeScore,
                      g->detail.c_str());
    }
  }
  if (!ok) Serial.printf("[nfl] failed: %s\n", why.c_str());
  nfLastOk = ok;
}

void quakeWork() {
  float minMag, la, lo;
  int radius;
  bool have;
  {
    std::lock_guard<std::mutex> lock(mtx);
    minMag = quakeMin;
    radius = quakeRadius;
    la = lat;
    lo = lon;
    have = haveLoc;
  }
  BigVec<Quake> fresh;
  QuakeSummary sum;
  String why;
  bool ok;
  if (radius > 0 && !have) {
    ok = false;
    why = "\"near me\" needs a ZIP code: set one under Weather";
  } else {
    ok = fetchQuakes(minMag, radius, la, lo, fresh, sum, why);
  }
  if (ok) {
    // Yesterday's total, local midnight to midnight. Recounted every time:
    // USGS keeps adding late-reviewed quakes for a day or so.
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    lt.tm_hour = lt.tm_min = lt.tm_sec = 0;
    lt.tm_isdst = -1;
    time_t today0 = mktime(&lt);
    lt.tm_mday -= 1;
    lt.tm_isdst = -1;  // 23 or 25 h long on a daylight-saving day
    time_t yest0 = mktime(&lt);
    String cwhy;
    sum.yesterday = usgsCount(yest0, today0, usgsFilter(minMag, radius, la, lo), cwhy);
    if (sum.yesterday < 0) {
      Serial.printf("[quake] yesterday's count failed: %s\n", cwhy.c_str());
      std::lock_guard<std::mutex> lock(mtx);
      if (quakeSum.valid && quakeSum.yesterday >= 0) sum.yesterday = quakeSum.yesterday;  // keep the last one
    }
  }
  {
    std::lock_guard<std::mutex> lock(mtx);
    if (minMag == quakeMin && radius == quakeRadius) {  // ignore if the settings changed meanwhile
      if (ok) {
        sum.fetchedMs = millis();
        quakeList.swap(fresh);
        quakeSum = sum;
        quakeErr = "";
      } else {
        quakeErr = why;
      }
      quakeVer++;
    }
  }
  if (ok)
    Serial.printf("[quake] M%.1f+: %d today, %ld yesterday, %d in 24 h\n", minMag, sum.today, sum.yesterday, sum.h24);
  else
    Serial.printf("[quake] failed: %s\n", why.c_str());
  qkLastOk = ok;
}

void volcanoWork() {
  std::vector<Volcano> us;
  String why;
  bool ok = fetchUsVolcanoes(us, why);
  {
    std::lock_guard<std::mutex> lock(mtx);
    if (ok) {
      // Pop-ups for alert level / color changes (not for the first list after boot)
      if (volcBaseline && volcPop) {
        for (const auto &v : us) {
          auto it = std::find_if(volcList.begin(), volcList.end(),
                                 [&](const Volcano &o) { return !strcmp(o.vnum, v.vnum); });
          bool isNewOne = it == volcList.end();
          if (!isNewOne && !strcmp(it->alert, v.alert) && !strcmp(it->color, v.color)) continue;
          String t = String(v.name) + " volcano";
          String m;
          if (isNewOne) m = String("Now ") + v.color + " / " + v.alert;
          else {
            bool up = colorRankOf(v.color) * 10 + alertRankOf(v.alert) > colorRankOf(it->color) * 10 + alertRankOf(it->alert);
            m = String(up ? "Raised to " : "Lowered to ") + v.color + " / " + v.alert + "\n(was " + it->color + " / " +
                it->alert + ")";
          }
          m += String("\n") + v.obs;
          if (v.synopsis[0]) m += String("\n\n") + v.synopsis;
          volcPopQueue.push_back(t + "\n" + m);
        }
        for (const auto &o : volcList) {  // dropped off the elevated list
          bool still = std::any_of(us.begin(), us.end(), [&](const Volcano &v) { return !strcmp(o.vnum, v.vnum); });
          if (!still)
            volcPopQueue.push_back(String(o.name) + " volcano\nNo longer elevated (was " + o.color + " / " + o.alert +
                                   ")\n" + o.obs);
        }
        if (volcPopQueue.size() > 6) volcPopQueue.erase(volcPopQueue.begin(), volcPopQueue.end() - 6);
      }
      volcBaseline = true;
      volcList.swap(us);
      volcErr = "";
      volcFetchedMs = millis();
    } else {
      volcErr = why;
    }
    volcVer++;
  }
  if (ok) Serial.printf("[volcano] %u U.S. volcanoes elevated\n", (unsigned)volcList.size());
  else Serial.printf("[volcano] U.S. list failed: %s\n", why.c_str());
  vcLastOk = ok;

  // Worldwide weekly report, every few hours
  bool due = !eruptAttempted || millis() - eruptLastAttempt >= (eruptLastOk ? ERUPT_REFRESH_MS : ERUPT_RETRY_MS);
  if (!due) return;
  eruptAttempted = true;
  eruptLastAttempt = millis();
  BigVec<Eruption> world;
  String week, ewhy;
  bool eok = fetchEruptions(world, week, ewhy);
  {
    std::lock_guard<std::mutex> lock(mtx);
    if (eok) {
      eruptList.swap(world);
      eruptWeek = week;
      eruptErr = "";
      eruptFetchedMs = millis();
    } else {
      eruptErr = ewhy;
    }
    volcVer++;
  }
  if (!eok) Serial.printf("[volcano] weekly report failed: %s\n", ewhy.c_str());
  eruptLastOk = eok;
}

void flightsWork() {
  String why;
  bool ok = pollFlights(why);
  {
    std::lock_guard<std::mutex> lock(mtx);
    if (ok) {
      fltErr = "";
      fltFetchedMs = millis();
    } else {
      fltErr = why;
      fltVer++;
    }
  }
  if (!ok) Serial.printf("[flights] failed: %s\n", why.c_str());
  flLastOk = ok;
}

void fetchWork() {
  if (taskFlags & DO_FLIGHTS) flightsWork();
  if (taskFlags & DO_VOLCANO) volcanoWork();
  if (taskFlags & DO_QUAKE) quakeWork();
  if (taskFlags & DO_BUOY) buoyWork();
  if (taskFlags & DO_STOCK) stockWork();
  if (taskFlags & DO_NFL) nflWork();
  if (!(taskFlags & (DO_FORECAST | DO_ALERTS))) return;  // buoy/stock/NFL-only run
  String zip, place, why;
  float la, lo;
  bool have;
  {
    std::lock_guard<std::mutex> lock(mtx);
    zip = zipCode;
    la = lat;
    lo = lon;
    have = haveLoc;
    place = placeName;
  }
  uint8_t flags = taskFlags;
  bool doForecast = flags & DO_FORECAST;
  Serial.printf("[wx] fetching%s%s for %s, free heap %lu\n", doForecast ? " forecast" : "",
                (flags & DO_ALERTS) ? " alerts" : "", zip.c_str(), (unsigned long)ESP.getFreeHeap());

  bool ok = true;
  if (!have) {
    ok = geocode(zip, la, lo, place, why);
    if (ok) Serial.printf("[wx] %s -> %s (%.4f, %.4f)\n", zip.c_str(), place.c_str(), la, lo);
  }
  Data d;
  if (ok && doForecast) ok = fetchForecast(la, lo, d, why);

  // NWS alerts (needs a location; US only, other places just come back empty or 404)
  if ((flags & DO_ALERTS) && (have || ok)) {
    std::vector<Alert> fresh;
    String awhy;
    bool aok = fetchAlerts(la, lo, fresh, awhy);
    {
      std::lock_guard<std::mutex> lock(mtx);
      if (zip == zipCode) {
        if (aok) {
          applyAlerts(fresh);
          alertsFetchedMs = millis();
          alertsErr = "";
        } else {
          alertsErr = awhy;
        }
      }
    }
    if (aok) Serial.printf("[wx] %u active NWS alert(s)\n", (unsigned)alertList.size());
    else Serial.printf("[wx] NWS alerts failed: %s\n", awhy.c_str());
    alLastOk = aok;
  }
  if (!doForecast) return;  // alerts-only run: leave the forecast state alone

  {
    std::lock_guard<std::mutex> lock(mtx);
    if (zip == zipCode) {  // ignore results for a ZIP that was changed meanwhile
      if (!have && ok) {
        lat = la;
        lon = lo;
        placeName = place;
        haveLoc = true;
        locDirty = true;
      }
      if (ok) {
        cur = d;
        err = "";
      } else {
        err = why;
      }
    }
  }
  if (ok)
    Serial.printf("[wx] %.1f C, code %d, today %d (%.0f/%.0f C)\n", d.tempC, d.code, d.dayCode,
                  d.hiC, d.loC);
  else
    Serial.printf("[wx] failed: %s\n", why.c_str());
  lastOk = ok;
  st = ok ? Status::Idle : Status::Error;
}

void fetchTask(void *) {
  fetchWork();
  taskRunning = false;
  vTaskDelete(nullptr);
}

void startFetch(uint8_t flags) {
  taskFlags = flags;
  taskRunning = true;
  if (flags & DO_FORECAST) {
    st = Status::Busy;
    attempted = true;
    lastAttempt = millis();
  }
  if (flags & DO_ALERTS) {
    alAttempted = true;
    alLastAttempt = millis();
  }
  if (flags & DO_BUOY) {
    byAttempted = true;
    byLastAttempt = millis();
  }
  if (flags & DO_STOCK) {
    stAttempted = true;
    stLastAttempt = millis();
  }
  if (flags & DO_NFL) {
    nfAttempted = true;
    nfLastAttempt = millis();
  }
  if (flags & DO_QUAKE) {
    qkAttempted = true;
    qkLastAttempt = millis();
  }
  if (flags & DO_VOLCANO) {
    vcAttempted = true;
    vcLastAttempt = millis();
  }
  if (flags & DO_FLIGHTS) {
    flAttempted = true;
    flLastAttempt = millis();
  }
  if (xTaskCreatePinnedToCore(fetchTask, "weather", TASK_STACK, nullptr, 1, nullptr, 0) != pdPASS) {
    std::lock_guard<std::mutex> lock(mtx);
    taskRunning = false;
    if (flags & DO_FORECAST) {
      err = "not enough memory";
      st = Status::Error;
      lastOk = false;
    }
    if (flags & DO_ALERTS) {
      alertsErr = "not enough memory";
      alLastOk = false;
    }
    if (flags & DO_BUOY) {
      buoyErr = "not enough memory";
      byLastOk = false;
    }
    if (flags & DO_STOCK) {
      stockErr = "not enough memory";
      stLastOk = false;
    }
    if (flags & DO_NFL) {
      nflErr = "not enough memory";
      nfLastOk = false;
    }
    if (flags & DO_QUAKE) {
      quakeErr = "not enough memory";
      qkLastOk = false;
    }
    if (flags & DO_VOLCANO) {
      volcErr = "not enough memory";
      vcLastOk = false;
    }
    if (flags & DO_FLIGHTS) {
      fltErr = "not enough memory";
      flLastOk = false;
    }
  }
}

}  // namespace

#if defined(BOARD_NM_CYD_C5) && defined(MBEDTLS_PLATFORM_MEMORY) && !defined(MBEDTLS_PLATFORM_CALLOC_MACRO)
// TLS sessions (~50 KB each, with 16 KB record buffers) go to PSRAM
static void *tlsCalloc(size_t n, size_t size) {
  void *p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : calloc(n, size);
}
static void tlsFree(void *p) { heap_caps_free(p); }
#define HAVE_TLS_PSRAM 1
#endif

void begin() {
#if defined(HAVE_TLS_PSRAM)
  if (ESP.getPsramSize() && mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree) == 0) {
    tlsInPsram = true;
    Serial.println("[wx] HTTPS buffers in PSRAM");
  }
#endif
  prefs.begin("wx", false);  // read-write: creates the namespace on first boot
  zipCode = prefs.isKey("zip") ? prefs.getString("zip") : String();
  placeName = prefs.isKey("place") ? prefs.getString("place") : String();
  lat = prefs.getFloat("lat", 0);
  lon = prefs.getFloat("lon", 0);
  haveLoc = prefs.getBool("loc", false) && zipCode.length();
  unitF = prefs.getBool("unitF", true);
  iconOn = prefs.getBool("icon", true);
  String sn = prefs.isKey("asnz") ? prefs.getString("asnz") : String();
  buoyId = prefs.isKey("buoy") ? prefs.getString("buoy") : String(BUOY_DEFAULT);
  bool legacyOn = prefs.getBool("buoyOn", true);  // 0.7.0 had one switch
  buoyBar = prefs.getBool("buoyBar", legacyOn);
  buoyDesk = prefs.getBool("buoyDesk", legacyOn);
  alertDesk = prefs.getBool("alertDesk", true);
  {
    String v = prefs.isKey("stkSyms") ? prefs.getString("stkSyms")
               : prefs.isKey("stkSym") ? prefs.getString("stkSym") + "\n"  // 0.8.0 kept one ticker
                                       : String(STOCK_DEFAULT) + "\n";
    for (int i = 0; i < (int)v.length();) {
      int nl = v.indexOf('\n', i);
      if (nl < 0) nl = v.length();
      if (nl > i && stockSyms.size() < MAX_STOCKS) stockSyms.push_back(v.substring(i, nl));
      i = nl + 1;
    }
    for (const auto &x : stockSyms) {
      Stock st;
      st.symbol = x;
      stockList.push_back(st);
    }
  }
  stockBar = prefs.getBool("stkBar", true);
  nflTeamAbbr = prefs.isKey("nflTeam") ? prefs.getString("nflTeam") : String();
  nflBar = prefs.getBool("nflBar", true);
  nflDesk = prefs.getBool("nflDesk", true);
  stockDesk = prefs.getBool("stkDesk", true);
  quakeMin = prefs.isKey("eqMin") ? prefs.getFloat("eqMin", 2.5f) : 2.5f;
  quakeRadius = prefs.getInt("eqRad", 0);
  quakeDesk = prefs.getBool("eqDesk", true);
  volcDesk = prefs.getBool("vcDesk", true);
  volcPop = prefs.getBool("vcPop", true);
  fltDesk = prefs.getBool("flDesk", true);
  nearDesk = prefs.getBool("flNear", true);
  if (prefs.isKey("flAppr")) prefs.remove("flAppr");  // 1.6.2's approach widget is gone
  if (prefs.isKey("apCode")) {
    apCode = prefs.getString("apCode");
    apName = prefs.isKey("apName") ? prefs.getString("apName") : String();
    apHaveLoc = prefs.getBool("apLoc", false);
    if (apHaveLoc) {
      apLat = prefs.getFloat("apLat", 0);
      apLon = prefs.getFloat("apLon", 0);
      apElevFt = prefs.getInt("apElev", 0);
      apMagVar = prefs.getFloat("apMag", 0);
    }
  }
  fltDay = prefs.getUInt("flDay", 0);
  fltDepToday = prefs.getInt("flDep", 0);
  fltArrToday = prefs.getInt("flArr", 0);
  if (prefs.isKey("eqWk")) prefs.remove("eqWk");  // 1.4.0's weekly history is no longer kept
  String fv = prefs.isKey("bfav") ? prefs.getString("bfav") : String();
  for (int i = 0; i < (int)fv.length();) {
    int nl = fv.indexOf('\n', i);
    if (nl < 0) nl = fv.length();
    if (nl > i) buoyFavs.push_back(fv.substring(i, nl));
    i = nl + 1;
  }
  prefs.end();
  for (int i = 0; i < (int)sn.length();) {
    int nl = sn.indexOf('\n', i);
    if (nl < 0) nl = sn.length();
    if (nl > i) snoozedIds.push_back(sn.substring(i, nl));
    i = nl + 1;
  }
}

void loop() {
  if (apDirty) {
    apDirty = false;
    std::lock_guard<std::mutex> lock(mtx);
    prefs.begin("wx", false);
    prefs.putString("apName", apName);
    prefs.putFloat("apLat", apLat);
    prefs.putFloat("apLon", apLon);
    prefs.putInt("apElev", apElevFt);
    prefs.putFloat("apMag", apMagVar);
    prefs.putBool("apLoc", true);
    prefs.end();
  }
  if (locDirty) {
    locDirty = false;
    std::lock_guard<std::mutex> lock(mtx);
    prefs.begin("wx", false);
    prefs.putString("place", placeName);
    prefs.putFloat("lat", lat);
    prefs.putFloat("lon", lon);
    prefs.putBool("loc", true);
    prefs.end();
  }

  // Drop alerts whose end time has passed (between fetches)
  static uint32_t lastExpireCheck = 0;
  if (millis() - lastExpireCheck > 15000) {
    lastExpireCheck = millis();
    time_t now = time(nullptr);
    if (now > 1700000000) {
      std::lock_guard<std::mutex> lock(mtx);
      size_t n = alertList.size();
      alertList.erase(std::remove_if(alertList.begin(), alertList.end(),
                                     [&](const Alert &a) { return a.until && (time_t)a.until < now; }),
                      alertList.end());
      if (alertList.size() != n) alertsVer++;
    }
  }

  if (taskRunning || net::state() != net::State::Connected) return;
  // No downloads while the drone scanner or a Bluetooth tool is in use: they
  // share the radio and the Bluetooth stack holds memory HTTPS needs. Overdue
  // updates run as soon as Bluetooth is released (1 min after the tool stops).
  if (bluetoothBusy()) return;
  uint8_t flags = 0;
  if (configured()) {
    if (force || !attempted || millis() - lastAttempt >= (lastOk ? REFRESH_MS : RETRY_MS)) flags |= DO_FORECAST;
    if (millis() >= ALERTS_BOOT_DELAY_MS &&
        (alForce || !alAttempted || millis() - alLastAttempt >= (alLastOk ? ALERTS_REFRESH_MS : RETRY_MS)))
      flags |= DO_ALERTS;
  }
  // The buoy doesn't need a ZIP code. It runs on its own, after the first
  // forecast and alert checks, so two TLS downloads never overlap.
  if ((buoyBar || buoyDesk) && buoyId.length() && !flags && (millis() >= BUOY_BOOT_DELAY_MS || byForce) &&
      (byForce || !byAttempted || millis() - byLastAttempt >= (byLastOk ? BUOY_REFRESH_MS : BUOY_RETRY_MS)))
    flags |= DO_BUOY;
  // Stock quote: on its own, once nothing else is due
  if (!flags && (stockBar || stockDesk) && !stockSyms.empty() && (millis() >= STOCK_BOOT_DELAY_MS || stForce)) {
    uint32_t every = !stLastOk ? STOCK_RETRY_MS : usMarketHours() ? STOCK_REFRESH_MS : STOCK_CLOSED_MS;
    if (stForce || !stAttempted || millis() - stLastAttempt >= every) flags |= DO_STOCK;
  }
  // NFL: every minute while your team plays, every 2 min just before kickoff
  if (!flags && (nflBar || nflDesk) && (millis() >= NFL_BOOT_DELAY_MS || nfForce)) {
    uint32_t every = NFL_IDLE_MS;
    if (!nfLastOk) {
      every = NFL_RETRY_MS;
    } else {
      std::lock_guard<std::mutex> lock(mtx);
      if (const Game *g = teamGame()) {
        time_t now = time(nullptr);
        if (g->state == "in") every = NFL_LIVE_MS;
        else if (g->state == "pre" && g->kickoff && now > 1700000000 && (long)g->kickoff - (long)now < 30 * 60)
          every = NFL_SOON_MS;
      }
    }
    if (nfForce || !nfAttempted || millis() - nfLastAttempt >= every) flags |= DO_NFL;
  }
  // Earthquakes: on their own, once nothing else is due
  bool quakeWanted = quakeDesk || (quakeWantedMs && millis() - quakeWantedMs < QUAKE_WANTED_MS);
  if (!flags && quakeWanted && (millis() >= QUAKE_BOOT_DELAY_MS || qkForce) &&
      (qkForce || !qkAttempted || millis() - qkLastAttempt >= (qkLastOk ? QUAKE_REFRESH_MS : QUAKE_RETRY_MS)))
    flags |= DO_QUAKE;
  // Volcanoes: on their own, once nothing else is due
  bool volcWanted = volcDesk || volcPop || (volcWantedMs && millis() - volcWantedMs < QUAKE_WANTED_MS);
  if (!flags && volcWanted && (millis() >= VOLC_BOOT_DELAY_MS || vcForce) &&
      (vcForce || !vcAttempted || millis() - vcLastAttempt >= (vcLastOk ? VOLC_REFRESH_MS : VOLC_RETRY_MS)))
    flags |= DO_VOLCANO;
  // Airport flights: polled often, so lowest priority (only when nothing else is due)
  bool fltWanted = fltDesk || nearDesk || (fltWantedMs && millis() - fltWantedMs < QUAKE_WANTED_MS);
  if (!flags && fltWanted && (millis() >= FLT_BOOT_DELAY_MS || flForce) &&
      (flForce || !flAttempted || millis() - flLastAttempt >= (flLastOk ? FLT_POLL_MS : FLT_RETRY_MS)))
    flags |= DO_FLIGHTS;
  if (flags) {
    force = alForce = false;
    if (flags & DO_QUAKE) qkForce = false;
    if (flags & DO_FLIGHTS) flForce = false;
    if (flags & DO_VOLCANO) vcForce = false;
    if (flags & DO_BUOY) byForce = false;
    if (flags & DO_STOCK) stForce = false;
    if (flags & DO_NFL) nfForce = false;
    startFetch(flags);
  }
}

bool tlsActive() {
#if defined(BOARD_NM_CYD_C5)
  // Widgets live in PSRAM here, so screen updates can go on during HTTPS,
  // unless internal RAM still gets tight (TLS buffers are internal)
  return tlsBusy && heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 24 * 1024;
#else
  return tlsBusy;
#endif
}

bool configured() {
  std::lock_guard<std::mutex> lock(mtx);
  return zipCode.length() > 0;
}

String zip() {
  std::lock_guard<std::mutex> lock(mtx);
  return zipCode;
}

String place() {
  std::lock_guard<std::mutex> lock(mtx);
  return placeName;
}

static void quakeSettingsChanged();

void setZip(const String &z) {
  {
    std::lock_guard<std::mutex> lock(mtx);
    zipCode = z;
    haveLoc = false;
    placeName = "";
    cur = Data();
    err = "";
    alertList.clear();
    poppedIds.clear();
    popupQueue.clear();
    alertsErr = "";
    alertsFetchedMs = 0;
    alertsVer++;
    if (quakeRadius > 0) quakeSettingsChanged();  // "near me" counts belonged to the old place
  }
  prefs.begin("wx", false);
  prefs.putString("zip", z);
  prefs.putBool("loc", false);
  prefs.end();
  force = alForce = true;
}

void refresh() { force = alForce = true; }

bool useF() { return unitF; }

void setUseF(bool f) {
  unitF = f;
  prefs.begin("wx", false);
  prefs.putBool("unitF", f);
  prefs.end();
}

bool showIcon() { return iconOn; }

void setShowIcon(bool on) {
  iconOn = on;
  prefs.begin("wx", false);
  prefs.putBool("icon", on);
  prefs.end();
}

Data data() {
  std::lock_guard<std::mutex> lock(mtx);
  return cur;
}

Status status() { return st; }

String lastError() {
  std::lock_guard<std::mutex> lock(mtx);
  return err;
}

const lv_img_dsc_t *icon(const Data &d, bool large) {
#define WX_PICK(n) (large ? &wx_##n##_l : &wx_##n##_s)
  int c = d.dayCode;
  if (c >= 95) return WX_PICK(storm);
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return WX_PICK(snow);
  if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) return WX_PICK(rain);
  if (c >= 51 && c <= 57) return WX_PICK(drizzle);
  if (c == 45 || c == 48) return WX_PICK(fog);
  if (d.windMaxKmh >= 40) return WX_PICK(wind);  // ~25 mph on an otherwise dry day
  if (c == 3) return WX_PICK(cloud);
  if (c == 1 || c == 2) return WX_PICK(partly);
  return WX_PICK(sun);
#undef WX_PICK
}

const char *describe(int c) {
  switch (c) {
    case 0: return "Clear";
    case 1: return "Mostly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: case 81: return "Rain showers";
    case 82: return "Heavy showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Thunderstorm, hail";
    default: return "Unknown";
  }
}

int temp(float c) { return (int)lroundf(unitF ? c * 9.0f / 5.0f + 32.0f : c); }
int wind(float kmh) { return (int)lroundf(unitF ? kmh / 1.609f : kmh); }
const char *windUnit() { return unitF ? "mph" : "km/h"; }

// ---------- NWS alerts API ----------

int severityRank(const String &s) {
  if (s == "Extreme") return 4;
  if (s == "Severe") return 3;
  if (s == "Moderate") return 2;
  if (s == "Minor") return 1;
  return 0;
}

std::vector<Alert> alerts() {
  std::lock_guard<std::mutex> lock(mtx);
  return alertList;
}

bool findAlert(const String &id, Alert &out) {
  std::lock_guard<std::mutex> lock(mtx);
  for (const auto &a : alertList) {
    if (a.id == id) {
      out = a;
      return true;
    }
  }
  return false;
}

void snoozeAlert(const String &id, bool on) {
  std::lock_guard<std::mutex> lock(mtx);
  auto it = std::find(snoozedIds.begin(), snoozedIds.end(), id);
  if (on && it == snoozedIds.end()) {
    if (snoozedIds.size() >= MAX_SNOOZED) snoozedIds.erase(snoozedIds.begin());
    snoozedIds.push_back(id);
  } else if (!on && it != snoozedIds.end()) {
    snoozedIds.erase(it);
  } else {
    return;
  }
  for (auto &a : alertList)
    if (a.id == id) a.snoozed = on;
  popupQueue.erase(std::remove(popupQueue.begin(), popupQueue.end(), id), popupQueue.end());
  saveSnoozed();
  alertsVer++;
}

size_t alertCount() {
  std::lock_guard<std::mutex> lock(mtx);
  return alertList.size();
}

size_t unsnoozedAlertCount() {
  std::lock_guard<std::mutex> lock(mtx);
  size_t n = 0;
  for (const auto &a : alertList)
    if (!a.snoozed) n++;
  return n;
}

bool nextAlertPopup(String &id) {
  std::lock_guard<std::mutex> lock(mtx);
  while (!popupQueue.empty()) {
    id = popupQueue.front();
    popupQueue.erase(popupQueue.begin());
    for (const auto &a : alertList)
      if (a.id == id && !a.snoozed) return true;  // still active and not snoozed
  }
  return false;
}

uint32_t alertsVersion() { return alertsVer; }

String alertsStatus() {
  std::lock_guard<std::mutex> lock(mtx);
  if (zipCode.isEmpty()) return "Set a ZIP code to monitor NWS alerts.";
  if (bluetoothBusy()) return PAUSED_MSG;
  if (alertsErr.length()) return "NWS update failed: " + alertsErr;
  if (!alertsFetchedMs)
    return millis() < ALERTS_BOOT_DELAY_MS ? "First NWS check 30 s after startup..."
                                           : "Checking the National Weather Service...";
  char b[64];
  snprintf(b, sizeof(b), "NWS checked %lu min ago, every 5 min",
           (unsigned long)((millis() - alertsFetchedMs) / 60000));
  return b;
}

// ---------- NDBC buoy API ----------

namespace {
// Built-in list: wave-reporting buoys only (no tide gauges or met-only
// platforms). Names are short enough to fit the desktop widget.
const char *R_ATL = "Florida Atlantic coast";
const char *R_KEYS = "Florida Keys";
const char *R_FLGULF = "Florida Gulf coast";
const char *R_PAN = "Panhandle & Alabama";
const char *R_GULF = "Central & western Gulf";
const BuoyPreset PRESETS[] = {
    // Atlantic, north to south
    {"41112", "Fernandina Beach Offshore", R_ATL},
    {"41117", "St. Augustine", R_ATL},
    {"41012", "St. Augustine 40 NM ENE", R_ATL},
    {"41070", "Ponce de Leon Inlet", R_ATL},
    {"41009", "Canaveral 20 NM East", R_ATL},
    {"41113", "Cape Canaveral Nearshore", R_ATL},
    {"41114", "Fort Pierce", R_ATL},
    {"41122", "Hollywood Beach", R_ATL},
    {"42095", "Satan Shoal, Key West", R_KEYS},
    // Florida Gulf coast, south to north
    {"42097", "Pulley Ridge, SW Florida", R_FLGULF},
    {"42099", "Offshore St. Petersburg", R_FLGULF},
    {"42098", "Egmont Channel, Tampa Bay", R_FLGULF},
    {"42036", "West Tampa 112 NM WNW", R_FLGULF},
    // Panhandle and Alabama, east to west
    {"42039", "Pensacola 115 NM SSE", R_PAN},
    {"42012", "Orange Beach 44 NM SE", R_PAN},
    {"42040", "Dauphin Island 64 NM S", R_PAN},
    // Offshore and western Gulf, east to west
    {"42001", "Mid Gulf", R_GULF},
    {"42002", "West Gulf", R_GULF},
    {"42035", "Galveston 22 NM E", R_GULF},
    {"42019", "Freeport 60 NM S", R_GULF},
    {"42020", "Corpus Christi 60 NM SE", R_GULF},
};
const size_t MAX_BUOY_FAVS = 8;

void saveFavs() {  // caller holds mtx
  String s;
  for (const auto &f : buoyFavs) s += f + "\n";
  prefs.begin("wx", false);
  prefs.putString("bfav", s);
  prefs.end();
}
}

const BuoyPreset *buoyPresets(size_t &n) {
  n = sizeof(PRESETS) / sizeof(PRESETS[0]);
  return PRESETS;
}

bool isPreset(const String &id) {
  for (const auto &p : PRESETS)
    if (id == p.id) return true;
  return false;
}

std::vector<String> buoyFavorites() {
  std::lock_guard<std::mutex> lock(mtx);
  std::vector<String> out;  // a favorite that's now built in is listed once, as a preset
  for (const auto &f : buoyFavs)
    if (!isPreset(f)) out.push_back(f);
  return out;
}

void addBuoyFavorite(const String &id) {
  if (id.isEmpty() || isPreset(id)) return;  // built-in buoys are already listed
  std::lock_guard<std::mutex> lock(mtx);
  buoyFavs.erase(std::remove(buoyFavs.begin(), buoyFavs.end(), id), buoyFavs.end());
  buoyFavs.insert(buoyFavs.begin(), id);  // newest first
  if (buoyFavs.size() > MAX_BUOY_FAVS) buoyFavs.resize(MAX_BUOY_FAVS);
  saveFavs();
}

void removeBuoyFavorite(const String &id) {
  std::lock_guard<std::mutex> lock(mtx);
  size_t n = buoyFavs.size();
  buoyFavs.erase(std::remove(buoyFavs.begin(), buoyFavs.end(), id), buoyFavs.end());
  if (buoyFavs.size() != n) saveFavs();
}

String buoyName(const String &id) {
  for (const auto &p : PRESETS)
    if (id == p.id) return p.name;
  return "";
}

String buoyStation() {
  std::lock_guard<std::mutex> lock(mtx);
  return buoyId;
}

void setBuoyStation(const String &id) {
  {
    std::lock_guard<std::mutex> lock(mtx);
    if (id == buoyId && buoyData.valid) return;  // same station, data already here
    buoyId = id;
    buoyData = Buoy();
    buoyErr = "";
    buoyVer++;
  }
  prefs.begin("wx", false);
  prefs.putString("buoy", id);
  prefs.end();
  byForce = true;
}

bool buoyActive() { return buoyBar || buoyDesk; }
bool showBuoyBar() { return buoyBar; }
bool showBuoyDesk() { return buoyDesk; }

void setShowBuoyBar(bool on) {
  bool wasActive = buoyActive();
  buoyBar = on;
  prefs.begin("wx", false);
  prefs.putBool("buoyBar", on);
  prefs.end();
  if (!wasActive && on) byForce = true;
  buoyVer++;
}

void setShowBuoyDesk(bool on) {
  bool wasActive = buoyActive();
  buoyDesk = on;
  prefs.begin("wx", false);
  prefs.putBool("buoyDesk", on);
  prefs.end();
  if (!wasActive && on) byForce = true;
  buoyVer++;
}

bool showAlertDesk() { return alertDesk; }

void setShowAlertDesk(bool on) {
  alertDesk = on;
  prefs.begin("wx", false);
  prefs.putBool("alertDesk", on);
  prefs.end();
}

void refreshBuoy() { byForce = true; }

Buoy buoy() {
  std::lock_guard<std::mutex> lock(mtx);
  return buoyData;
}

uint32_t buoyVersion() { return buoyVer; }

String buoyStatus() {
  std::lock_guard<std::mutex> lock(mtx);
  if (!buoyBar && !buoyDesk) return "Buoy monitoring is off (both wave displays are switched off).";
  if (bluetoothBusy()) return PAUSED_MSG;
  if (buoyErr.length()) return "Buoy update failed: " + buoyErr;
  if (!buoyData.valid)
    return millis() < BUOY_BOOT_DELAY_MS ? "First buoy check 45 s after startup..." : "Getting buoy data from NOAA...";
  char b[64];
  snprintf(b, sizeof(b), "Checked %lu min ago, every 30 min - NOAA NDBC",
           (unsigned long)((millis() - buoyData.fetchedMs) / 60000));
  return b;
}

String waveText(float m) {
  if (std::isnan(m)) return "--";
  char b[16];
  if (unitF) {
    long in = lroundf(m * 39.3701f);
    snprintf(b, sizeof(b), "%ld' %ld\"", in / 12, in % 12);
  } else {
    snprintf(b, sizeof(b), "%.1f m", m);
  }
  return b;
}

// ---------- Stock quote API ----------

size_t stockCount() {
  std::lock_guard<std::mutex> lock(mtx);
  return stockSyms.size();
}

std::vector<String> stockSymbols() {
  std::lock_guard<std::mutex> lock(mtx);
  return stockSyms;
}

bool stockAt(size_t i, Stock &out) {
  std::lock_guard<std::mutex> lock(mtx);
  if (i >= stockList.size()) return false;
  out = stockList[i];
  return true;
}

bool findStock(const String &sym, Stock &out) {
  std::lock_guard<std::mutex> lock(mtx);
  for (const auto &st : stockList)
    if (st.symbol == sym) {
      out = st;
      return true;
    }
  return false;
}

std::vector<Stock> stocks() {
  std::lock_guard<std::mutex> lock(mtx);
  return stockList;
}

bool addStockSymbol(const String &sym, String &why) {
  {
    std::lock_guard<std::mutex> lock(mtx);
    if (std::find(stockSyms.begin(), stockSyms.end(), sym) != stockSyms.end()) {
      why = sym + " is already in the list.";
      return false;
    }
    if (stockSyms.size() >= MAX_STOCKS) {
      why = "Up to 8 tickers. Remove one first.";
      return false;
    }
    stockSyms.push_back(sym);
    Stock st;
    st.symbol = sym;
    stockList.push_back(st);
    saveStockSyms();
    stockVer++;
  }
  stForce = true;
  return true;
}

void removeStockSymbol(const String &sym) {
  std::lock_guard<std::mutex> lock(mtx);
  for (size_t i = 0; i < stockSyms.size(); i++)
    if (stockSyms[i] == sym) {
      stockSyms.erase(stockSyms.begin() + i);
      stockList.erase(stockList.begin() + i);
      saveStockSyms();
      stockVer++;
      return;
    }
}

bool stockActive() { return stockBar || stockDesk; }
bool showStockBar() { return stockBar; }
bool showStockDesk() { return stockDesk; }

void setShowStockBar(bool on) {
  bool was = stockActive();
  stockBar = on;
  prefs.begin("wx", false);
  prefs.putBool("stkBar", on);
  prefs.end();
  if (!was && on) stForce = true;
  stockVer++;
}

void setShowStockDesk(bool on) {
  bool was = stockActive();
  stockDesk = on;
  prefs.begin("wx", false);
  prefs.putBool("stkDesk", on);
  prefs.end();
  if (!was && on) stForce = true;
  stockVer++;
}

void refreshStock() { stForce = true; }

uint32_t stockVersion() { return stockVer; }

String stockStatus() {
  std::lock_guard<std::mutex> lock(mtx);
  if (!stockBar && !stockDesk) return "Stock tracking is off (both stock displays are switched off).";
  if (stockSyms.empty()) return "Add a ticker to start tracking.";
  if (bluetoothBusy()) return PAUSED_MSG;
  if (stockErr.length()) return "Quote update failed: " + stockErr;
  if (!stockFetchedMs)
    return millis() < STOCK_BOOT_DELAY_MS ? "First quotes 1 min after startup..." : "Getting quotes...";
  char b[96];
  snprintf(b, sizeof(b), "Checked %lu min ago, every %s - Yahoo Finance (may be delayed)",
           (unsigned long)((millis() - stockFetchedMs) / 60000), usMarketHours() ? "5 min" : "30 min (market closed)");
  return b;
}

float stockChangePct(const Stock &s) {
  if (!s.valid || std::isnan(s.prevClose) || s.prevClose <= 0) return NAN;
  return (s.price - s.prevClose) / s.prevClose * 100.0f;
}

// ---------- NFL API ----------

namespace {
const char *const NFL_TEAMS[][2] = {
    {"ARI", "Arizona Cardinals"},    {"ATL", "Atlanta Falcons"},      {"BAL", "Baltimore Ravens"},
    {"BUF", "Buffalo Bills"},        {"CAR", "Carolina Panthers"},    {"CHI", "Chicago Bears"},
    {"CIN", "Cincinnati Bengals"},   {"CLE", "Cleveland Browns"},     {"DAL", "Dallas Cowboys"},
    {"DEN", "Denver Broncos"},       {"DET", "Detroit Lions"},        {"GB", "Green Bay Packers"},
    {"HOU", "Houston Texans"},       {"IND", "Indianapolis Colts"},   {"JAX", "Jacksonville Jaguars"},
    {"KC", "Kansas City Chiefs"},    {"LV", "Las Vegas Raiders"},     {"LAC", "Los Angeles Chargers"},
    {"LAR", "Los Angeles Rams"},     {"MIA", "Miami Dolphins"},       {"MIN", "Minnesota Vikings"},
    {"NE", "New England Patriots"},  {"NO", "New Orleans Saints"},    {"NYG", "New York Giants"},
    {"NYJ", "New York Jets"},        {"PHI", "Philadelphia Eagles"},  {"PIT", "Pittsburgh Steelers"},
    {"SF", "San Francisco 49ers"},   {"SEA", "Seattle Seahawks"},     {"TB", "Tampa Bay Buccaneers"},
    {"TEN", "Tennessee Titans"},     {"WSH", "Washington Commanders"},
};
}

size_t nflTeamCount() { return sizeof(NFL_TEAMS) / sizeof(NFL_TEAMS[0]); }
const char *nflTeamAbbrAt(size_t i) { return i < nflTeamCount() ? NFL_TEAMS[i][0] : ""; }
const char *nflTeamNameAt(size_t i) { return i < nflTeamCount() ? NFL_TEAMS[i][1] : ""; }

String nflTeam() {
  std::lock_guard<std::mutex> lock(mtx);
  return nflTeamAbbr;
}

void setNflTeam(const String &abbr) {
  {
    std::lock_guard<std::mutex> lock(mtx);
    nflTeamAbbr = abbr;
    nflVer++;
  }
  prefs.begin("wx", false);
  prefs.putString("nflTeam", abbr);
  prefs.end();
  nfForce = true;
}

bool nflActive() { return nflBar || nflDesk; }
bool showNflBar() { return nflBar; }
bool showNflDesk() { return nflDesk; }

void setShowNflBar(bool on) {
  bool was = nflActive();
  nflBar = on;
  prefs.begin("wx", false);
  prefs.putBool("nflBar", on);
  prefs.end();
  if (!was && on) nfForce = true;
  nflVer++;
}

void setShowNflDesk(bool on) {
  bool was = nflActive();
  nflDesk = on;
  prefs.begin("wx", false);
  prefs.putBool("nflDesk", on);
  prefs.end();
  if (!was && on) nfForce = true;
  nflVer++;
}

void refreshNfl() { nfForce = true; }

std::vector<Game> nflGamesList() {
  std::lock_guard<std::mutex> lock(mtx);
  return nflGames;
}

bool nflTeamGame(Game &out) {
  std::lock_guard<std::mutex> lock(mtx);
  const Game *g = teamGame();
  if (!g) return false;
  out = *g;
  return true;
}

uint32_t nflVersion() { return nflVer; }

String nflStatus() {
  std::lock_guard<std::mutex> lock(mtx);
  if (!nflBar && !nflDesk) return "NFL scores are off (both displays are switched off).";
  if (bluetoothBusy()) return PAUSED_MSG;
  if (nflErr.length()) return "Score update failed: " + nflErr;
  if (!nflFetchedMs)
    return millis() < NFL_BOOT_DELAY_MS ? "First scores about a minute after startup..." : "Getting scores...";
  const Game *g = teamGame();
  const char *pace = g && g->state == "in" ? "every minute (game on)" : "every 30 min, every minute during your game";
  char b[112];
  snprintf(b, sizeof(b), "Checked %lu min ago, %s - ESPN", (unsigned long)((millis() - nflFetchedMs) / 60000), pace);
  return b;
}

// ---------- Earthquakes ----------

QuakeSummary quakeSummary() {
  std::lock_guard<std::mutex> lock(mtx);
  return quakeSum;
}

BigVec<Quake> quakes(uint32_t sinceEpoch, size_t maxN) {
  std::lock_guard<std::mutex> lock(mtx);
  BigVec<Quake> out;
  for (const auto &q : quakeList) {
    if (q.time < sinceEpoch) break;  // newest first
    if (out.size() >= maxN) break;
    out.push_back(q);
  }
  return out;
}

float quakeMinMag() { return quakeMin; }
int quakeRadiusKm() { return quakeRadius; }
bool showQuakeDesk() { return quakeDesk; }

// A new filter makes the old numbers meaningless: clear them and refetch
static void quakeSettingsChanged() {  // caller holds mtx
  quakeList.clear();
  quakeSum = QuakeSummary();
  quakeErr = "";
  quakeVer++;
  qkForce = true;
}

void setQuakeMinMag(float m) {
  std::lock_guard<std::mutex> lock(mtx);
  if (m == quakeMin) return;
  quakeMin = m;
  prefs.begin("wx", false);
  prefs.putFloat("eqMin", m);
  prefs.end();
  quakeSettingsChanged();
}

void setQuakeRadiusKm(int km) {
  std::lock_guard<std::mutex> lock(mtx);
  if (km == quakeRadius) return;
  quakeRadius = km;
  prefs.begin("wx", false);
  prefs.putInt("eqRad", km);
  prefs.end();
  quakeSettingsChanged();
}

void setShowQuakeDesk(bool on) {
  std::lock_guard<std::mutex> lock(mtx);
  quakeDesk = on;
  prefs.begin("wx", false);
  prefs.putBool("eqDesk", on);
  prefs.end();
  if (on && !quakeSum.valid) qkForce = true;
  quakeVer++;
}

void refreshQuake() {
  quakeWantedMs = millis();
  qkForce = true;
}

void quakeKeepAlive() { quakeWantedMs = millis(); }

uint32_t quakeVersion() { return quakeVer; }


String quakeStatus() {
  std::lock_guard<std::mutex> lock(mtx);
  if (bluetoothBusy()) return PAUSED_MSG;
  if (net::state() != net::State::Connected && !quakeSum.valid) return "Waiting for Wi-Fi...";
  if (quakeErr.length()) return "Update failed: " + quakeErr;
  if (!quakeSum.valid)
    return millis() < QUAKE_BOOT_DELAY_MS && !qkForce ? "First check about 90 s after startup..."
                                                      : "Getting earthquakes from USGS...";
  char b[96];
  snprintf(b, sizeof(b), "Checked %lu min ago, every 10 min - USGS",
           (unsigned long)((millis() - quakeSum.fetchedMs) / 60000));
  return b;
}

bool quakeDistanceKm(const Quake &q, float &km) {
  float la, lo;
  {
    std::lock_guard<std::mutex> lock(mtx);
    if (!haveLoc || std::isnan(q.lat) || std::isnan(q.lon)) return false;
    la = lat;
    lo = lon;
  }
  const float R = 6371.0f, d2r = (float)M_PI / 180.0f;
  float dLat = (q.lat - la) * d2r, dLon = (q.lon - lo) * d2r;
  float a = sinf(dLat / 2) * sinf(dLat / 2) + cosf(la * d2r) * cosf(q.lat * d2r) * sinf(dLon / 2) * sinf(dLon / 2);
  km = 2 * R * atan2f(sqrtf(a), sqrtf(1 - a));
  return true;
}

// ---------- Volcanoes ----------

std::vector<Volcano> usVolcanoes() {
  std::lock_guard<std::mutex> lock(mtx);
  return volcList;
}

BigVec<Eruption> eruptions() {
  std::lock_guard<std::mutex> lock(mtx);
  return eruptList;
}

String eruptionWeek() {
  std::lock_guard<std::mutex> lock(mtx);
  return eruptWeek;
}

int colorRank(const char *color) { return colorRankOf(color); }
bool showVolcanoDesk() { return volcDesk; }
bool volcanoPopups() { return volcPop; }

void setShowVolcanoDesk(bool on) {
  std::lock_guard<std::mutex> lock(mtx);
  volcDesk = on;
  prefs.begin("wx", false);
  prefs.putBool("vcDesk", on);
  prefs.end();
  if (on && !volcFetchedMs) vcForce = true;
  volcVer++;
}

void setVolcanoPopups(bool on) {
  std::lock_guard<std::mutex> lock(mtx);
  volcPop = on;
  prefs.begin("wx", false);
  prefs.putBool("vcPop", on);
  prefs.end();
  if (!on) volcPopQueue.clear();
}

bool nextVolcanoPopup(String &title, String &msg) {
  std::lock_guard<std::mutex> lock(mtx);
  if (volcPopQueue.empty()) return false;
  String s = volcPopQueue.front();
  volcPopQueue.erase(volcPopQueue.begin());
  int nl = s.indexOf('\n');
  title = nl < 0 ? s : s.substring(0, nl);
  msg = nl < 0 ? String("") : s.substring(nl + 1);
  return true;
}

void refreshVolcano() {
  volcWantedMs = millis();
  eruptAttempted = false;  // the weekly report too
  vcForce = true;
}

void volcanoKeepAlive() { volcWantedMs = millis(); }
uint32_t volcanoVersion() { return volcVer; }

String volcanoStatus() {
  std::lock_guard<std::mutex> lock(mtx);
  if (bluetoothBusy()) return PAUSED_MSG;
  if (volcErr.length()) return "U.S. update failed: " + volcErr;
  if (!volcFetchedMs)
    return millis() < VOLC_BOOT_DELAY_MS && !vcForce ? "First check about 2 min after startup..."
                                                     : "Getting alert levels from USGS...";
  char b[80];
  snprintf(b, sizeof(b), "USGS checked %lu min ago, every 15 min", (unsigned long)((millis() - volcFetchedMs) / 60000));
  return b;
}

String eruptionStatus() {
  std::lock_guard<std::mutex> lock(mtx);
  if (eruptErr.length()) return "Weekly report failed: " + eruptErr;
  if (!eruptFetchedMs) return "Getting the weekly report...";
  char b[96];
  unsigned long m = (millis() - eruptFetchedMs) / 60000;
  if (m < 120) snprintf(b, sizeof(b), "Smithsonian report checked %lu min ago", m);
  else snprintf(b, sizeof(b), "Smithsonian report checked %lu h ago", m / 60);
  return b;
}

bool volcanoDistanceKm(float vlat, float vlon, float &km) {
  if (std::isnan(vlat) || std::isnan(vlon)) return false;
  Quake q;
  q.lat = vlat;
  q.lon = vlon;
  return quakeDistanceKm(q, km);
}

// ---------- Airport flights ----------

std::vector<FlightEvent> flightDepartures() {
  std::lock_guard<std::mutex> lock(mtx);
  return fltDeps;
}
std::vector<FlightEvent> flightArrivals() {
  std::lock_guard<std::mutex> lock(mtx);
  return fltArrs;
}
std::vector<LiveFlight> liveFlights() {
  std::lock_guard<std::mutex> lock(mtx);
  return fltLive;
}
// Today's counts (0 once the day has rolled over, even before the next poll)
int flightsDepToday() {
  std::lock_guard<std::mutex> lock(mtx);
  return time(nullptr) > 1700000000 && localDay(time(nullptr)) != fltDay ? 0 : fltDepToday;
}
int flightsArrToday() {
  std::lock_guard<std::mutex> lock(mtx);
  return time(nullptr) > 1700000000 && localDay(time(nullptr)) != fltDay ? 0 : fltArrToday;
}
String airportCode() {
  std::lock_guard<std::mutex> lock(mtx);
  return apCode;
}
String airportName() {
  std::lock_guard<std::mutex> lock(mtx);
  return apName;
}

void setAirportCode(const String &code) {
  String c = code;
  c.trim();
  c.toUpperCase();
  if (c.length() < 3 || c.length() > 4) return;
  std::lock_guard<std::mutex> lock(mtx);
  if (c == apCode) return;
  apCode = c;
  apName = "";
  apHaveLoc = false;
  fltDeps.clear();
  fltArrs.clear();
  fltLive.clear();
  fltNearTotal = -1;
  fltTracks.clear();  // only touched by the task, which checks apCode before committing
  fltDepToday = fltArrToday = 0;
  fltErr = "";
  fltVer++;
  prefs.begin("wx", false);
  prefs.putString("apCode", c);
  prefs.putBool("apLoc", false);
  prefs.putInt("flDep", 0);
  prefs.putInt("flArr", 0);
  prefs.end();
  flForce = true;
}

bool showFlightDesk() { return fltDesk; }
bool showNearbyDesk() { return nearDesk; }

void setShowNearbyDesk(bool on) {
  std::lock_guard<std::mutex> lock(mtx);
  nearDesk = on;
  prefs.begin("wx", false);
  prefs.putBool("flNear", on);
  prefs.end();
  if (on) flForce = true;
  fltVer++;
}

int flightsNearTotal() {
  std::lock_guard<std::mutex> lock(mtx);
  return fltNearTotal;
}

void setShowFlightDesk(bool on) {
  std::lock_guard<std::mutex> lock(mtx);
  fltDesk = on;
  prefs.begin("wx", false);
  prefs.putBool("flDesk", on);
  prefs.end();
  if (on) flForce = true;
  fltVer++;
}

void refreshFlights() {
  fltWantedMs = millis();
  flForce = true;
}

void flightsKeepAlive() { fltWantedMs = millis(); }
uint32_t flightsVersion() { return fltVer; }

String flightsStatus() {
  std::lock_guard<std::mutex> lock(mtx);
  if (bluetoothBusy()) return PAUSED_MSG;
  if (net::state() != net::State::Connected && !fltFetchedMs) return "Waiting for Wi-Fi...";
  if (fltErr.length()) return "Update failed: " + fltErr;
  if (!apHaveLoc) return "Looking up " + apCode + "...";
  if (!fltFetchedMs)
    return millis() < FLT_BOOT_DELAY_MS && !flForce ? "Live tracking starts 2 min after startup..."
                                                    : "Getting live aircraft data...";
  char b[96];
  unsigned long s = (millis() - fltFetchedMs) / 1000;
  snprintf(b, sizeof(b), "Live: updated %lu s ago, every %lu s", s, (unsigned long)(FLT_POLL_MS / 1000));
  return b;
}

const char *aircraftName(const char *code) {
  for (const auto &t : TYPES)
    if (!strcmp(code, t.code)) return t.name;
  return code;
}

}  // namespace weather
