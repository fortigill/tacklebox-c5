#include "weather.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>

#include "ble.h"
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
bool tlsFits(String &why) {
  if (ESP.getMaxAllocHeap() >= TLS_MIN_BLOCK && ESP.getFreeHeap() >= TLS_MIN_FREE) return true;
  why = String("skipped, low memory (") + ESP.getFreeHeap() / 1024 + " KB free, " + ESP.getMaxAllocHeap() / 1024 +
        " KB block). Bluetooth tools hold memory; it is released 1 min after they stop";
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
const char *STOCK_TMP = "/stk.json";
const char *STOCK_DEFAULT = "SPY";

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
String stockSym;
bool stockBar = true, stockDesk = true;
Stock stockData;
String stockErr;
uint32_t stockVer = 0;

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
volatile bool taskRunning = false;
volatile bool tlsBusy = false;  // an HTTPS session is open (holds ~40 KB of heap)
struct TlsGuard {               // marks the scope of a TLS session for the UI
  TlsGuard() { tlsBusy = true; }
  ~TlsGuard() { tlsBusy = false; }
};
enum : uint8_t { DO_FORECAST = 1, DO_ALERTS = 2, DO_BUOY = 4, DO_STOCK = 8 };

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
      if (!sz) break;  // last chunk
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
  http.setUserAgent("CYD-UI/" FW_VERSION " (ESP32 touch display; personal weather widget)");
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
    http.setUserAgent("CYD-UI/" FW_VERSION " (ESP32 touch display; personal weather widget)");
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

bool downloadStock(const String &sym, String &why, int &httpCode) {
  TlsGuard guard;  // declared first, released after the TLS session is gone
  WiFiClientSecure tls;
  HTTPClient http;
  tls.setInsecure();  // public, read-only data
  http.setTimeout(15000);
  http.setConnectTimeout(15000);
  http.setReuse(false);
  String url = "https://query1.finance.yahoo.com/v8/finance/chart/" + urlEncodeSymbol(sym) + "?range=1d&interval=1d";
  if (!http.begin(tls, url)) {
    why = "bad URL";
    return false;
  }
  http.setUserAgent("Mozilla/5.0 (compatible; CYD-UI/" FW_VERSION ")");
  const char *hdrs[] = {"Transfer-Encoding"};
  http.collectHeaders(hdrs, 1);
  httpCode = http.GET();
  Serial.printf("[stock] %s -> %d (free %lu, largest block %lu)\n", sym.c_str(), httpCode,
                (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap());
  if (httpCode != 200 && httpCode != 404) {  // 404 still carries a JSON error message
    why = httpCode < 0 ? http.errorToString(httpCode) : String("HTTP ") + httpCode;
    http.end();
    return false;
  }
  File f = SPIFFS.open(STOCK_TMP, FILE_WRITE);
  if (!f) {
    why = "can't write to flash";
    http.end();
    return false;
  }
  const bool chunked = http.header("Transfer-Encoding").equalsIgnoreCase("chunked");
  String swhy;
  long got = streamBody(http, chunked, f, 32 * 1024, swhy);
  f.close();
  http.end();
  tls.stop();
  if (got <= 0) {
    why = got < 0 ? "download failed: " + swhy : String("empty response");
    return false;
  }
  return true;
}

bool fetchStock(const String &sym, Stock &out, String &why) {
  if (!fsReady()) {
    why = "flash storage unavailable";
    return false;
  }
  if (!tlsFits(why)) return false;
  int code = 0;
  bool ok = downloadStock(sym, why, code);  // TLS is closed when this returns
  if (!ok) {
    SPIFFS.remove(STOCK_TMP);
    return false;
  }
  File f = SPIFFS.open(STOCK_TMP, FILE_READ);
  if (!f) {
    why = "saved quote missing";
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
  SPIFFS.remove(STOCK_TMP);
  if (e) {
    why = String("bad quote data: ") + e.c_str();
    return false;
  }
  const char *err = doc["chart"]["error"]["description"] | "";
  JsonObject m = doc["chart"]["result"][0]["meta"];
  if (m.isNull() || !m["regularMarketPrice"].is<float>()) {
    why = code == 404 || *err ? "ticker " + sym + " not found" : String("no price in the reply");
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
  return true;
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
  String sym;
  {
    std::lock_guard<std::mutex> lock(mtx);
    sym = stockSym;
  }
  Stock s;
  String why;
  bool ok = fetchStock(sym, s, why);
  {
    std::lock_guard<std::mutex> lock(mtx);
    if (sym == stockSym) {  // ignore if the ticker was changed meanwhile
      if (ok) {
        stockData = s;
        stockErr = "";
      } else {
        stockErr = why;
      }
      stockVer++;
    }
  }
  if (ok) Serial.printf("[stock] %s %.2f (prev close %.2f)\n", sym.c_str(), s.price, s.prevClose);
  else Serial.printf("[stock] %s failed: %s\n", sym.c_str(), why.c_str());
  stLastOk = ok;
}

void fetchWork() {
  if (taskFlags & DO_BUOY) buoyWork();
  if (taskFlags & DO_STOCK) stockWork();
  if (!(taskFlags & (DO_FORECAST | DO_ALERTS))) return;  // buoy/stock-only run
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
  }
}

}  // namespace

void begin() {
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
  stockSym = prefs.isKey("stkSym") ? prefs.getString("stkSym") : String(STOCK_DEFAULT);
  stockBar = prefs.getBool("stkBar", true);
  stockDesk = prefs.getBool("stkDesk", true);
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
  if (!flags && (stockBar || stockDesk) && stockSym.length() && (millis() >= STOCK_BOOT_DELAY_MS || stForce)) {
    uint32_t every = !stLastOk ? STOCK_RETRY_MS : usMarketHours() ? STOCK_REFRESH_MS : STOCK_CLOSED_MS;
    if (stForce || !stAttempted || millis() - stLastAttempt >= every) flags |= DO_STOCK;
  }
  if (flags) {
    force = alForce = false;
    if (flags & DO_BUOY) byForce = false;
    if (flags & DO_STOCK) stForce = false;
    startFetch(flags);
  }
}

bool tlsActive() { return tlsBusy; }

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
// Built-in list, north to south along the Florida Atlantic coast, then the Keys
const BuoyPreset PRESETS[] = {
    // Short enough to fit the desktop widget
    {"41112", "Fernandina Beach Offshore"},
    {"41117", "St. Augustine"},
    {"41070", "Ponce de Leon Inlet"},
    {"41009", "Canaveral 20 NM East"},
    {"41012", "St. Augustine 40 NM ENE"},
    {"41113", "Cape Canaveral Nearshore"},
    {"41114", "Fort Pierce"},
    {"41122", "Hollywood Beach"},
    {"42095", "Satan Shoal, Key West"},
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
  return buoyFavs;
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

String stockSymbol() {
  std::lock_guard<std::mutex> lock(mtx);
  return stockSym;
}

void setStockSymbol(const String &sym) {
  {
    std::lock_guard<std::mutex> lock(mtx);
    if (sym == stockSym && stockData.valid) return;
    stockSym = sym;
    stockData = Stock();
    stockErr = "";
    stockVer++;
  }
  prefs.begin("wx", false);
  prefs.putString("stkSym", sym);
  prefs.end();
  stForce = true;
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

Stock stock() {
  std::lock_guard<std::mutex> lock(mtx);
  return stockData;
}

uint32_t stockVersion() { return stockVer; }

String stockStatus() {
  std::lock_guard<std::mutex> lock(mtx);
  if (!stockBar && !stockDesk) return "Stock tracking is off (both stock displays are switched off).";
  if (bluetoothBusy()) return PAUSED_MSG;
  if (stockErr.length()) return "Quote update failed: " + stockErr;
  if (!stockData.valid)
    return millis() < STOCK_BOOT_DELAY_MS ? "First quote 1 min after startup..." : "Getting the quote...";
  char b[96];
  snprintf(b, sizeof(b), "Checked %lu min ago, every %s - Yahoo Finance (may be delayed)",
           (unsigned long)((millis() - stockData.fetchedMs) / 60000), usMarketHours() ? "5 min" : "30 min (market closed)");
  return b;
}

float stockChangePct(const Stock &s) {
  if (!s.valid || std::isnan(s.prevClose) || s.prevClose <= 0) return NAN;
  return (s.price - s.prevClose) / s.prevClose * 100.0f;
}

}  // namespace weather
