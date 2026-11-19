#pragma once

#include <stddef.h>
#include <stdint.h>

// Wall clock: SNTP sync against pool.ntp.org once Wi-Fi is up (lwIP then
// re-syncs hourly by itself), time zone and 12/24 h preference in NVS.
namespace timesvc {

struct Zone {
  const char *name;
  const char *tz;  // POSIX TZ string (handles daylight saving)
};

void begin();  // load preferences and apply the time zone
void loop();   // starts SNTP the first time Wi-Fi is connected

bool valid();               // clock holds a real date (synced now or before a soft reset)
uint32_t lastSyncMs();      // millis() of the last successful sync, 0 = never this boot
bool started();             // SNTP has been started (Wi-Fi came up)
void syncNow();
const char *server();

bool use24h();
void setUse24h(bool on);

const Zone *zones(size_t *count);
size_t zoneIndex();
void setZone(size_t index);

// Formatting helpers; return "--:--" / "" style placeholders until valid()
void formatTime(char *buf, size_t n);  // "3:07 PM" or "15:07"
void formatDate(char *buf, size_t n);  // "Wed Nov 18 2026"

}  // namespace timesvc
