#include "weather.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <algorithm>
#include <mutex>

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
const size_t MAX_DESC = 700, MAX_INSTR = 300;
const uint32_t TLS_MIN_BLOCK = 32 * 1024;  // don't start an HTTPS fetch with less than this
const size_t MAX_SNOOZED = 16;
const uint16_t HTTP_TIMEOUT_MS = 8000;

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

// Scheduling (main task only)
uint32_t lastAttempt = 0;
bool attempted = false;
bool lastOk = false;
bool force = false;
uint32_t alLastAttempt = 0;
bool alAttempted = false;
bool alLastOk = false;
bool alForce = false;
volatile bool taskRunning = false;
enum : uint8_t { DO_FORECAST = 1, DO_ALERTS = 2 };
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
    if (secure) {
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

// ---- Minimal streaming JSON reader for the NWS alert list ----
// NWS alert texts can be several KB each. Rather than build a JSON document,
// read the stream character by character, keep only the fields we show and
// truncate them while reading, so memory stays small and bounded.

int rd(Stream &in) {  // next byte, waiting up to 10 s
  uint32_t t0 = millis();
  for (;;) {
    int c = in.read();
    if (c >= 0) return c;
    if (millis() - t0 > 10000) return -1;
    delay(1);
  }
}

int pk(Stream &in) {  // peek, waiting up to 10 s
  uint32_t t0 = millis();
  for (;;) {
    if (in.available()) return in.peek();
    if (millis() - t0 > 10000) return -1;
    delay(1);
  }
}

bool isWs(int c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

int nextNonWs(Stream &in) {
  int c;
  do c = rd(in);
  while (isWs(c));
  return c;
}

int peekNonWs(Stream &in) {
  int c;
  while (isWs(c = pk(in))) in.read();
  return c;
}

// Reads a JSON string body (opening quote already consumed), keeping at most
// `limit` characters (0 = discard). Returns false on timeout/EOF.
bool readStr(Stream &in, String &out, size_t limit, bool *truncated = nullptr) {
  out = "";
  if (truncated) *truncated = false;
  for (;;) {
    int c = rd(in);
    if (c < 0) return false;
    if (c == '"') return true;
    if (c == '\\') {
      int e = rd(in);
      if (e < 0) return false;
      switch (e) {
        case 'n': c = '\n'; break;
        case 't': c = ' '; break;
        case 'r': c = 0; break;
        case 'b':
        case 'f': c = 0; break;
        case 'u': {
          char hex[5] = {0};
          for (int i = 0; i < 4; i++) {
            int h = rd(in);
            if (h < 0) return false;
            hex[i] = (char)h;
          }
          long cp = strtol(hex, nullptr, 16);
          c = cp < 0x80 ? (int)cp : '?';  // the UI fonts are ASCII-only
          break;
        }
        default: c = e; break;  // \" \\ \/
      }
    }
    if (!c) continue;
    if (out.length() < limit) out += (char)c;
    else if (limit && truncated) *truncated = true;
  }
}

// Skips one JSON value (first character not yet consumed)
bool skipValue(Stream &in) {
  int c = peekNonWs(in);
  if (c < 0) return false;
  if (c == '"') {
    in.read();
    String dummy;
    return readStr(in, dummy, 0);
  }
  if (c == '{' || c == '[') {
    in.read();
    int depth = 1;
    while (depth > 0) {
      int d = rd(in);
      if (d < 0) return false;
      if (d == '"') {
        String dummy;
        if (!readStr(in, dummy, 0)) return false;
      } else if (d == '{' || d == '[') {
        depth++;
      } else if (d == '}' || d == ']') {
        depth--;
      }
    }
    return true;
  }
  // number / true / false / null: read up to the next delimiter
  for (;;) {
    int d = pk(in);
    if (d < 0) return false;
    if (d == ',' || d == '}' || d == ']' || isWs(d)) return true;
    in.read();
  }
}

size_t fieldLimit(const String &k) {
  if (k == "description") return MAX_DESC;
  if (k == "instruction") return MAX_INSTR;
  if (k == "headline" || k == "areaDesc") return 200;
  if (k == "id") return 120;
  if (k == "event" || k == "senderName") return 64;
  if (k == "onset" || k == "expires" || k == "ends") return 40;
  if (k == "severity" || k == "urgency" || k == "certainty" || k == "status") return 16;
  return 0;  // not needed
}

// Reads the alerts out of a /alerts/active response
bool parseAlerts(Stream &in, std::vector<Alert> &out, String &why) {
  while (out.size() < MAX_ALERTS && in.find("\"properties\"")) {
    if (nextNonWs(in) != ':' || nextNonWs(in) != '{') {
      why = "unexpected alert data";
      return false;
    }
    Alert a;
    String status = "Actual", onset, expires, ends;
    for (;;) {
      int c = nextNonWs(in);
      if (c == '}') break;
      if (c == ',') continue;
      String key;
      if (c != '"' || !readStr(in, key, 24) || nextNonWs(in) != ':') {
        why = "unexpected alert data";
        return false;
      }
      size_t lim = fieldLimit(key);
      if (lim && peekNonWs(in) == '"') {
        in.read();
        String v;
        bool cut = false;
        if (!readStr(in, v, lim, &cut)) {
          why = "alert download interrupted";
          return false;
        }
        if (cut) v += "...";
        if (key == "id") a.id = v;
        else if (key == "event") a.event = v;
        else if (key == "headline") a.headline = v;
        else if (key == "severity") a.severity = v;
        else if (key == "urgency") a.urgency = v;
        else if (key == "certainty") a.certainty = v;
        else if (key == "areaDesc") a.area = v;
        else if (key == "description") a.description = v;
        else if (key == "instruction") a.instruction = v;
        else if (key == "senderName") a.sender = v;
        else if (key == "status") status = v;
        else if (key == "onset") onset = v;
        else if (key == "expires") expires = v;
        else if (key == "ends") ends = v;
      } else if (!skipValue(in)) {
        why = "alert download interrupted";
        return false;
      }
    }
    if (status != "Actual" || a.id.isEmpty()) continue;  // skip tests and exercises
    if (a.event.isEmpty()) a.event = "Weather alert";
    if (a.severity.isEmpty()) a.severity = "Unknown";
    a.onset = parseIso(onset.c_str());
    a.until = parseIso(ends.length() ? ends.c_str() : expires.c_str());
    out.push_back(a);
  }
  return true;
}

bool fetchAlerts(float la, float lo, std::vector<Alert> &out, String &why) {
  if (ESP.getMaxAllocHeap() < TLS_MIN_BLOCK) {  // TLS would fail; say why instead
    why = String("not enough free RAM for HTTPS (largest block ") + ESP.getMaxAllocHeap() / 1024 +
          " KB, free " + ESP.getFreeHeap() / 1024 + " KB)";
    return false;
  }
  // Same declaration order rule as httpGet(): clients must outlive `http`
  WiFiClientSecure tls;
  HTTPClient http;
  tls.setInsecure();  // public, read-only data
  http.setTimeout(10000);
  http.setConnectTimeout(10000);
  http.useHTTP10(true);  // no chunked encoding, so the JSON can be parsed straight off the stream
  char url[128];
  snprintf(url, sizeof(url), "https://api.weather.gov/alerts/active?point=%.4f,%.4f", la, lo);
  if (!http.begin(tls, url)) {
    why = "bad URL";
    return false;
  }
  http.setUserAgent("CYD-UI/" FW_VERSION " (ESP32 touch display; personal weather widget)");
  http.addHeader("Accept", "application/geo+json");
  uint32_t heap = ESP.getFreeHeap(), block = ESP.getMaxAllocHeap();
  int code = http.GET();
  if (code != 200) {
    if (code < 0) {
      // HTTPClient reports any failed connect (incl. a TLS handshake) as
      // "connection refused"; add the TLS library's own reason and memory
      char tlsErr[80] = "";
      tls.lastError(tlsErr, sizeof(tlsErr));
      why = http.errorToString(code);
      if (tlsErr[0]) why += String(" (TLS: ") + tlsErr + ")";
      why += String(", free RAM ") + heap / 1024 + " KB, largest block " + block / 1024 + " KB";
    } else {
      why = String("HTTP ") + code;
    }
    http.end();
    return false;
  }
  Stream &in = http.getStream();
  bool ok = parseAlerts(in, out, why);
  http.end();
  if (!ok && out.empty()) return false;
  if (!ok) Serial.printf("[wx] NWS: kept %u alert(s) before: %s\n", (unsigned)out.size(), why.c_str());
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
void fetchWork() {
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

  if (taskRunning || !configured() || net::state() != net::State::Connected) return;
  uint8_t flags = 0;
  if (force || !attempted || millis() - lastAttempt >= (lastOk ? REFRESH_MS : RETRY_MS)) flags |= DO_FORECAST;
  if (alForce || !alAttempted || millis() - alLastAttempt >= (alLastOk ? ALERTS_REFRESH_MS : RETRY_MS))
    flags |= DO_ALERTS;
  if (flags) {
    force = alForce = false;
    startFetch(flags);
  }
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
  if (alertsErr.length()) return "NWS update failed: " + alertsErr;
  if (!alertsFetchedMs) return "Checking the National Weather Service...";
  char b[64];
  snprintf(b, sizeof(b), "NWS checked %lu min ago, every 5 min",
           (unsigned long)((millis() - alertsFetchedMs) / 60000));
  return b;
}

}  // namespace weather
