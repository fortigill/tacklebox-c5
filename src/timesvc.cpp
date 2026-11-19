#include "timesvc.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_sntp.h>
#include <time.h>

#include "net.h"

namespace timesvc {
namespace {

const char *NTP_PRIMARY = "pool.ntp.org";
const char *NTP_SECONDARY = "1.pool.ntp.org";
const time_t VALID_AFTER = 1700000000;  // Nov 2023: anything earlier means "never set"

const Zone ZONES[] = {
    {"UTC", "UTC0"},
    {"US Eastern (New York)", "EST5EDT,M3.2.0,M11.1.0"},
    {"US Central (Chicago)", "CST6CDT,M3.2.0,M11.1.0"},
    {"US Mountain (Denver)", "MST7MDT,M3.2.0,M11.1.0"},
    {"US Arizona (Phoenix)", "MST7"},
    {"US Pacific (Los Angeles)", "PST8PDT,M3.2.0,M11.1.0"},
    {"US Alaska (Anchorage)", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"US Hawaii (Honolulu)", "HST10"},
    {"Atlantic (Halifax)", "AST4ADT,M3.2.0,M11.1.0"},
    {"Newfoundland (St. John's)", "NST3:30NDT,M3.2.0,M11.1.0"},
    {"Brazil (Sao Paulo)", "<-03>3"},
    {"UK / Ireland (London)", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Central Europe (Berlin)", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Eastern Europe (Athens)", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Moscow", "MSK-3"},
    {"Gulf (Dubai)", "<+04>-4"},
    {"India (Kolkata)", "IST-5:30"},
    {"China / Singapore", "CST-8"},
    {"Japan / Korea", "JST-9"},
    {"Australia East (Sydney)", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia Queensland", "AEST-10"},
    {"New Zealand (Auckland)", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
};
const size_t ZONE_COUNT = sizeof(ZONES) / sizeof(ZONES[0]);
const size_t DEFAULT_ZONE = 1;  // US Eastern

Preferences prefs;
size_t zone = DEFAULT_ZONE;
bool h24 = false;
bool sntpStarted = false;
volatile uint32_t lastSync = 0;

void onSync(struct timeval *) {  // runs on the lwIP task
  lastSync = millis();
}

void applyZone() {
  setenv("TZ", ZONES[zone].tz, 1);
  tzset();
}

void startSntp() {
  sntp_set_time_sync_notification_cb(onSync);
  // configTzTime (re)starts SNTP and applies TZ; calling it again forces a fresh sync
  configTzTime(ZONES[zone].tz, NTP_PRIMARY, NTP_SECONDARY);
  sntpStarted = true;
  Serial.printf("[time] SNTP started (%s), zone %s\n", NTP_PRIMARY, ZONES[zone].name);
}

}  // namespace

void begin() {
  // Read-write open creates the namespace on first boot; read-only on a
  // missing namespace/key just logs NOT_FOUND errors.
  prefs.begin("time", false);
  String tz = prefs.isKey("tz") ? prefs.getString("tz") : String(ZONES[DEFAULT_ZONE].tz);
  h24 = prefs.getBool("h24", false);
  prefs.end();
  zone = DEFAULT_ZONE;
  for (size_t i = 0; i < ZONE_COUNT; i++)
    if (tz == ZONES[i].tz) zone = i;
  applyZone();
}

void loop() {
  if (!sntpStarted && net::state() == net::State::Connected) startSntp();

  static uint32_t reported = 0;
  if (lastSync != reported) {
    reported = lastSync;
    char t[24], d[24];
    formatTime(t, sizeof(t));
    formatDate(d, sizeof(d));
    Serial.printf("[time] synced: %s %s\n", d, t);
  }
}

bool valid() { return time(nullptr) > VALID_AFTER; }
uint32_t lastSyncMs() { return lastSync; }
bool started() { return sntpStarted; }
const char *server() { return NTP_PRIMARY; }

void syncNow() {
  if (net::state() == net::State::Connected) startSntp();
}

bool use24h() { return h24; }

void setUse24h(bool on) {
  h24 = on;
  prefs.begin("time", false);
  prefs.putBool("h24", h24);
  prefs.end();
}

const Zone *zones(size_t *count) {
  if (count) *count = ZONE_COUNT;
  return ZONES;
}

size_t zoneIndex() { return zone; }

void setZone(size_t index) {
  if (index >= ZONE_COUNT) return;
  zone = index;
  applyZone();
  prefs.begin("time", false);
  prefs.putString("tz", ZONES[zone].tz);
  prefs.end();
}

void formatTime(char *buf, size_t n) {
  if (!valid()) {
    snprintf(buf, n, "--:--");
    return;
  }
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  if (h24) {
    snprintf(buf, n, "%02d:%02d", t.tm_hour, t.tm_min);
  } else {
    int h = t.tm_hour % 12;
    snprintf(buf, n, "%d:%02d %s", h ? h : 12, t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
  }
}

void formatEpoch(uint32_t epoch, char *buf, size_t n) {
  if (!epoch) {
    snprintf(buf, n, "unknown (clock not set)");
    return;
  }
  time_t t = epoch;
  struct tm tm;
  localtime_r(&t, &tm);
  char day[12];
  strftime(day, sizeof(day), "%b %d", &tm);
  if (h24) {
    snprintf(buf, n, "%s %02d:%02d", day, tm.tm_hour, tm.tm_min);
  } else {
    int h = tm.tm_hour % 12;
    snprintf(buf, n, "%s %d:%02d %s", day, h ? h : 12, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
  }
}

void formatDate(char *buf, size_t n) {
  if (!valid()) {
    snprintf(buf, n, "Date not set");
    return;
  }
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  strftime(buf, n, "%a %b %d %Y", &t);
}

}  // namespace timesvc
