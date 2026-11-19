# Changelog

All notable changes to this project. Versions follow [Semantic Versioning](https://semver.org/);
the version string lives in `src/version.h`.

## [0.7.2] - 2026-11-19

### Changed
- The status bar wave reading now sits to the left of the temperature and weather icon instead
  of between the weather and the clock.
- The desktop wave and alert readouts are now separate rounded "bubbles" with a border, a small
  ">" and press feedback, so they read as buttons.

### Fixed
- Overlapping text in the OTA "Firmware update" pop-up. The countdown was a second label that
  LVGL's msgbox drew on top of the message. It's now part of the message text and reads
  "Declining automatically in N s".

## [0.7.1] - 2026-11-19

### Added
- The wave height in the status bar: a wave icon plus the number, next to the weather. Tap it
  for the buoy page.
- A desktop alert-count readout next to the wave height ("5 alerts", red while any are
  unsnoozed). Tap it for the alert list, and Back returns to the desktop. Switch:
  **Weather > Alert count on desktop**.
- A wave icon (`ui_wave`, `ui_wave_s`, from `tools/gen_ui_icons.py`).

### Changed
- The desktop wave readout shows the wave icon instead of the word "Waves".
- The buoy page has two switches, **Waves in status bar** and **Waves on desktop**: both,
  either or neither. The buoy is only polled while at least one is on. The 0.7.0 setting
  carries over to both.
- The status bar spacing is a little tighter in portrait to fit the wave reading.

## [0.7.0] - 2026-11-19

### Added
- NOAA buoy data (NDBC):
  - **Desktop:** "Waves 4' 7"" below the status bar. Tap it for the buoy page. It shows meters
    in Celsius mode.
  - **Weather > Buoy data:** a page with the significant wave height, wave direction,
    dominant/average period, wind and gusts (kt), water and air temperature, pressure, and the
    observation time (waves are reported about hourly, wind every 10 min, so each value comes
    from the newest row that has it).
  - **Choose buoy:** 41009 Canaveral 20 NM E (the default), 41113 Cape Canaveral Nearshore,
    41010 Canaveral East, 41114 Fort Pierce, 41012 St. Augustine, or any station ID typed in.
  - The "Show waves on desktop" switch also turns monitoring on and off.
- How it's fetched: from `www.ndbc.noaa.gov/data/realtime2/<id>.txt`. Plain HTTP is tried
  first, then HTTPS. Lines are parsed as they arrive with a 200-byte buffer and the read stops
  after about 8 KB, so the ~600 KB file never sits in RAM. It refreshes every 30 min (retries
  after 5 min), with the first check 45 s after boot, and never runs at the same time as the
  forecast or alert downloads. The buoy works without a ZIP code.

## [0.6.4] - 2026-11-19

### Changed
- The connected Wi-Fi icon is blue again. The green wasn't bright enough on this panel.
- The OTA approval buttons now just say **Accept** and **Decline**.

## [0.6.3] - 2026-11-19

### Changed
- The desktop Wi-Fi icon is green when connected (was blue). Amber still means
  connecting/lost, and grey means off.

## [0.6.2] - 2026-11-19

### Fixed
- `pio run -e cyd-ota` failed with `KeyError: 'tool-espotapy'`. `tools/espota_patch.py` now
  skips packages the platform doesn't define and uses the framework's `tools/espota.py`.

## [0.6.1] - 2026-11-19

### Changed
- The OTA approval countdown is now 30 s (was 8 s). Stock espota.py waits a hard-coded 10 s for
  the device to connect back (its `--timeout` option covers only the invitation).
  `tools/espota_patch.py` generates `.pio/espota_cyd.py` with a 45 s wait, and the cyd-ota env
  uploads through it (`upload_protocol = custom`). The framework's file isn't modified.

## [0.6.0] - 2026-11-19

### Added
- Approval for over-the-air updates. When firmware (or a filesystem image) is pushed with
  `pio run -e cyd-ota -t upload`, the device shows "Firmware update" with **Accept update** and
  **Do not allow**, plus an 8-second countdown.
  - **Accept update:** the update runs as before, with a progress screen and a restart.
  - **Do not allow**, or no answer in time: the update is aborted before anything is written,
    the upload fails on the computer, the running firmware stays, and the device shows
    "Update refused".
  - The countdown is short because espota.py only waits about 10 s for the device to connect
    back after the invitation.

## [0.5.9] - 2026-11-19

### Fixed
- "download cut off (28 of 31 KB)" when the download was actually complete. The file size was
  read before the last buffered KB had been flushed to SPIFFS. The size is now checked after the
  file is closed.

### Notes
- 0.5.8 confirmed low heap as the cause of the "read Timeout". With 15 KB more free, NWS replied
  in 2.5 s. The TLS session uses about 56 KB at peak, leaving about 11 KB free during the
  download.

## [0.5.8] - 2026-11-19

### Changed
- Freed 15 KB of heap by dropping LVGL's second draw buffer. `flushCb()` sends pixels
  synchronously, so the second buffer was never in use. The display looks and behaves the same.
- Diagnostics: the NWS alert download logs free heap and the largest block before TLS, after the
  reply, and the all-time minimum.

### Notes
- Under investigation: the board times out waiting for NWS ("read Timeout") while a Mac sending
  identical headers gets the reply in 0.2 s. Low heap during the TLS session is suspected (Wi-Fi
  receive buffers can't be allocated).

## [0.5.7] - 2026-11-19

### Fixed
- NWS alerts failing with "read Timeout". The board sent HTTP/1.0 requests, which
  api.weather.gov's CDN answered slowly or not at all, while a normal HTTP/1.1 request returns
  in about 0.2 s. Alerts now use HTTP/1.1, and `HTTPClient::writeToStream` decodes chunked replies
  straight into the SPIFFS file.
- The serial log now shows the NWS reply code, the response time and the size, to make problems
  easier to diagnose.

## [0.5.6] - 2026-11-19

### Fixed
- Crash (LoadProhibited in `lv_list_add_btn` from `showWeather`) when a screen was opened during
  the NWS alert download. LVGL allocates from the same heap as the ~40 KB TLS session, and a new
  screen is built while the old one still exists, so the heap ran out. Screen changes now wait
  until any HTTPS download finishes (at most 20 s) and show "Updating weather alerts..." meanwhile.
  This was likely also the "unstable" behavior seen with 0.5.1-0.5.3.

### Notes
- "SPIFFS: mount failed, -10025" on the first boot after 0.5.5 is expected: the empty partition
  is formatted automatically and the message doesn't come back.

## [0.5.5] - 2026-11-19

### Fixed
- NWS alerts failing with "bad alert data: IncompleteInput". The download was cut off while the
  JSON was being parsed straight off the slow TLS connection. Alerts are now fetched in two steps:
  1. Download: the response is saved to the SPIFFS partition (up to 96 KB) with 15 s
     connect/read timeouts and a 20 s stall limit. Then the TLS connection is closed, freeing its
     ~40 KB.
  2. Parse: the saved file is parsed with no connection open, then deleted.
- Error messages are clearer: "download cut off (x of y KB)", "alert data too large",
  "connection stalled".

### Changed
- The full alert description and safety instructions come back when the largest free RAM block
  is at least 48 KB at parse time. Otherwise only the summary fields are kept, as in 0.5.4.

## [0.5.4] - 2026-11-19

### Reverted
- 0.5.1-0.5.3 (Bluetooth unloading via `NimBLEDevice::deinit`, the streaming alert parsers, the
  smaller LVGL draw buffers). The board became unstable with them. The code is back to 0.5.0 plus
  the changes below.

### Changed
- NWS alerts are lighter and gentler:
  - The first check waits until 30 s after boot, so it doesn't compete with startup, Wi-Fi and the
    first forecast download.
  - The long `description`/`instruction` texts (several KB each) are no longer downloaded, since
    holding them during the TLS session ran out of memory. Alerts keep the event, headline,
    severity/urgency/certainty, onset/end time, areas and issuing office. The detail page points to
    weather.gov for the full text.
  - HTTPS isn't attempted when the largest free block is under 36 KB (e.g. after Bluetooth tools
    have been used). The status explains the skip instead of showing "connection refused".

### Notes
- Open-Meteo, the forecast source, has no official warnings. NWS (`api.weather.gov`) remains the
  free official source for US alerts, and it is HTTPS-only.

## [0.5.0] - 2026-11-19

### Added
- **NWS weather alerts** for the weather ZIP code, from the National Weather Service API
  (`api.weather.gov/alerts/active?point=lat,lon`):
  - Covers the city/county/zone alerts for that location. Checked every 5 min while Wi-Fi is up
    (2 min after a failure). Test and exercise messages are skipped. US locations only.
  - A **new alert pops up** on any screen (Close / View). View opens the alert's details.
  - The **desktop weather icon flashes** (red temperature) while any active alert isn't snoozed. It
    is shown even if "Show icon on desktop" is off.
  - **Weather > Active alerts (n)** below the desktop-icon switch lists alerts until they expire,
    colored by severity (red Extreme/Severe, orange Moderate, amber Minor).
  - Alert detail shows the headline, severity/urgency/certainty, onset and end time, areas, full
    description, "What to do" instructions and the issuing office.
  - **Snooze alert** / **Unsnooze** per alert. Snoozed alerts stop the flashing and don't pop up.
    Snoozes are saved across reboots and forgotten once the alert is gone.
  - Alerts drop off at their end time (`ends`, or `expires`), even between fetches.
  - Parsed with an ArduinoJson field filter straight from the HTTP stream (HTTP/1.0, so no chunked
    encoding). Descriptions are capped at 900 chars and instructions at 450; max 8 alerts.

### Fixed
- Every weather fetch leaked a little heap. The background task's local Strings were never
  destroyed because `vTaskDelete()` doesn't return. The work now runs in a function that returns
  before the task deletes itself. This mattered more now that alerts are fetched every 5 min.

## [0.4.5] - 2026-11-19

### Changed
- **Drone Watch keeps scanning while a drone is in range.** A watch scan runs at least 5 s and then
  continues for as long as Remote ID is heard. It rests only after 8 s of silence
  (`WATCH_LINGER_MS`), which gives weak or distant drones time to deliver every message type.
  - During an extended scan the log is updated every 5 s, so alerts aren't held back until the drone
    leaves. Sightings now count these 5 s checks ("heard in N watch checks").
  - The status line shows "drone in range, scanning until it's gone". Wi-Fi stays paused for as long
    as a drone is around.

### Added
- The watch log stores the drone's **Operator ID** and **Self-ID description** and shows them in the
  log detail. "- (not broadcast)" means the drone didn't send one. The Operator ID message is optional
  under US (FAA) rules and many US-market drones omit it.

### Notes
- The log record grew, so it uses a new NVS key (`log2`). Entries logged by 0.4.0-0.4.4 are dropped
  on upgrade.

## [0.4.4] - 2026-11-19

### Changed
- Removed the 30-minute ignore/snooze. The drone alert pop-up now has **Close** and **View**. Either
  one acknowledges that drone: no repeat pop-ups while it stays around. It alerts again once it has
  been gone ~65 s and returns, when Watch is restarted, or after it is deleted from the log. Other
  drones alert right away. The status-bar icon still flashes blue/red whenever a drone is heard.

## [0.4.3] - 2026-11-19

### Fixed
- A drone stopped alerting after its first pop-up even when it wasn't ignored. Both buttons snoozed
  it for 30 minutes, and neither deleting it from the log nor restarting Watch cleared the snooze.
  The rules are now:
  - **Ignore 30 min**: the only action that snoozes, for 30 minutes.
  - **View**: acknowledges the alert. No repeats while the drone stays around, but it alerts again
    once it has been gone for 2 watch cycles (~65 s) and returns.
  - **Starting Watch** clears acknowledgments, so each session alerts fresh. Snoozes are
    time-based and still apply.
  - **Deleting a drone from the log** (or Delete all) forgets its snooze, acknowledgment and any
    queued alert.

## [0.4.2] - 2026-11-19

### Added
- **Drone Watch status icon** in the desktop status bar, between the weather and the clock:
  - Shown only while Watch is running, and gone when it stops.
  - Steady in the theme's text color while watching; flashes blue/red while a drone has been heard
    within the last watch cycle.
  - Tap it to open Drone RID.
  - A tintable alpha-only quadcopter image generated by `tools/gen_ui_icons.py`.
- **Snoozable drone alerts**, replacing the one-time "new drone logged" pop-up:
  - Every watch scan queues the drones it heard. A "New drone detected" pop-up (ID, DJI label,
    aircraft type, signal, sightings, first seen) appears on any screen, one at a time.
  - **Ignore 30 min** or **View** (opens the log entry). Either one snoozes alerts for that drone
    for 30 minutes.
  - A different drone alerts right away, with the same options.
  - Drones are identified by Remote ID (falling back to MAC), so rotating Bluetooth addresses don't
    cause repeat alerts. Pending alerts are cleared when Watch stops.
- `drone::logFind` and `logDelete` accept a Remote ID as well as a MAC.

## [0.4.1] - 2026-11-19

### Fixed
- Status text above lists was hidden. For example, on Network settings, "Connected: <network>
  (<IP>)" wrapped onto more lines, but the network list started at a fixed position and covered
  everything after the first line. This was worst in portrait (240 px wide). Network settings, the
  Bluetooth scanner, the tracker detector, Drone RID and the Wi-Fi analyzer now use a column
  layout: info lines take the height their wrapped text needs, and the list fills the space below
  (`makeBody` / `bodyLabel` / `bodyList`).
- Header titles are pinned to one line and end in "..." when too long, instead of wrapping out of
  the title bar.

### Reverted
- The half-screen menu drawer from 0.4.0. The main menu is a full screen again.

## [0.4.0] - 2026-11-19

### Added
- **Drone Watch** mode in **Drone RID**: a repeating cycle of 5 s scanning and 25 s resting, until
  stopped.
  - Wi-Fi reconnects during each rest, so the clock, weather and OTA keep working. It is only paused
    during the 5 s scans.
  - Every drone heard goes into a **watch log saved in NVS**, which survives reboots until you delete
    it. Each entry records the ID (DJI labeled), aircraft type, first/last seen time, how many watch
    scans it was heard in, last position and altitude, operator position and distance, last/best
    signal, transport and MAC. Entries are matched by Remote ID, then by MAC. Up to 40 are kept, and
    the oldest is dropped when full.
  - **Log** screen with per-entry detail, **Delete from log**, and **Delete all**, each with a
    confirmation.
  - A pop-up on any screen when Watch logs a drone it hasn't seen before.
  - Rear LED: cyan while Watch rests, blue while it scans, blinking blue while a drone is heard.
  - Flash writes are throttled: immediately for a new drone, otherwise at most every 5 min and on
    stop.
- Drone RID screen now has **Scan** (continuous, as before) | **Watch** | **Log (n)** buttons.
  Hunting by signal requires Scan mode.
- `timesvc::formatEpoch()` for stored timestamps (follows the 12/24 h setting).

### Changed
- The **main menu is a drawer** that slides over the live desktop. It covers about half the screen
  (two-thirds in portrait) and keeps the status bar (weather, clock, Wi-Fi) visible. Tap the dimmed
  area to close it. Back buttons that used to go to the menu reopen the drawer.

## [0.3.1] - 2026-11-18

### Fixed
- All colors were inverted on this board's panel: Dark looked light, Light looked dark, and the
  connected Wi-Fi icon (blue #2196F3) showed as orange-yellow (#DE690C). The ILI9341 is now put into
  display-inversion mode (`invertDisplay`). The setting is on by default, saved in NVS, and can be
  toggled under **Display & touch > Colors > Invert colors (panel fix)** for panels that don't need
  it.

### Notes
- "Swap red/blue" (0.0.3) was based on a wrong guess about the yellow icon. Leave it off unless
  colors are still wrong after the inversion fix.

## [0.3.0] - 2026-11-18

### Added
- **Color themes** under **Menu > Display & touch > Theme**, saved across reboots:
  - **Dark**: the original look.
  - **Light**: soft grey background, white panels, dark text.
  - **Miami Vice**: midnight purple with hot pink and aqua neon.
  - **Cyber Hacker**: black terminal with phosphor-green text and buttons (black button text).
  - Built as a child theme on top of LVGL's default theme (`src/theme.cpp`). It sets screen,
    panel, list section, button text and keyboard colors. A preview is in `tools/themes_preview.png`.

### Fixed
- The temperature degree ring and the Wi-Fi analyzer channel numbers were hard-coded white
  (invisible on Light). They now use the theme's text color.

## [0.2.1] - 2026-11-18

### Fixed
- OTA upload of 0.2.0 failed with "No response from device". `firmware.bin` was 1,974,912 bytes,
  larger than the 1,966,080-byte OTA slot, so the device refused it. PlatformIO's 99% "Flash" figure
  counts ELF sections, not the larger image.
  - Weather icons are now 16-color indexed images (`LV_IMG_CF_INDEXED_4BIT`) instead of true color
    + alpha, with the same look: 132 KB down to 23 KB.
  - New post-build check `tools/check_size.py` reports the real `.bin` size against the OTA slot.
    It fails the build if the image won't fit and warns under 64 KB of headroom.

## [0.2.0] - 2026-11-18

### Added
- **Drone Remote ID scanner** (**Menu > Drone Remote ID**). It decodes standard Open Drone ID
  (ASTM F3411 / ASD-STAN EN 4709-002) broadcasts, which include DJI's built-in Remote ID:
  - **Wi-Fi beacons** with the Remote ID vendor IE (OUI FA:0B:BC or 90:3A:E6, type 0x0D). DJI,
    Skydio, Parrot and most add-on modules use this. Wi-Fi goes into promiscuous mode and hops
    channels 1-13, with every other stop on channel 6.
  - **Wi-Fi NaN** service discovery frames for `org.opendroneid.remoteid`.
  - **Bluetooth 4 legacy** adverts (service data 0xFFFA), through a new NimBLE `Rid` scan mode.
  - Decodes Basic ID (serial / registration / session ID, aircraft type), Location (position,
    geodetic and barometric altitude, height, speed, heading, climb rate, status), System (operator
    position), Operator ID, Self-ID text, and Message Packs. DJI serials (CTA-2063 manufacturer code
    `1581F`) are labeled.
  - **Start runs until you tap Stop**, even if you leave the screen. Drones are listed most recent
    first, greyed out after 30 s of silence, with emergency status in red.
  - The detail view shows the operator's distance from the drone. Units follow the weather F/C
    setting (ft and mph vs m and km/h).
  - **Hunt by signal** reuses the hunt screen. For Wi-Fi drones it stops hopping and stays on the
    drone's channel.
  - Rear LED: solid blue while scanning, blinking blue while a drone is being heard, back to green
    on Stop.
- `net::suspend()` / `net::resume()`: the station connection is paused while the sniffer owns the
  radio and reconnects to the saved network afterwards. The desktop shows "Drone scan running - Wi-Fi
  paused".

### Changed
- Network settings, the Bluetooth scanner, the tracker detector and the Wi-Fi analyzer are blocked
  (with an explanation) while the drone scanner is running, since they need the same radios.

### Not supported
- Bluetooth 5 Long Range Remote ID: the original ESP32 has no coded PHY.
- DJI's proprietary DroneID / AeroScope signal: it is carried on OcuSync, which needs an SDR.

## [0.1.1] - 2026-11-18

### Added
- Boot status on the rear RGB LED:
  - Solid red as soon as the firmware starts.
  - Blinks red while booting (5 Hz, driven by an `esp_timer`, so it keeps blinking through setup).
  - Solid green once the board is up: setup is done and Wi-Fi has connected, failed, or has no
    saved network, with a 20 s cap.
  - New `src/led.cpp` drives the active-low LED on GPIO 4/16/17 and supports all 7 colors plus blink.

## [0.1.0] - 2026-11-18

### Changed
- **Bluetooth moved from the Arduino (Bluedroid) BLE library to NimBLE-Arduino 2.x.** It uses far
  less flash and RAM. NimBLE delivers every advertisement without the duplicate-report leak, so:
  - scans no longer restart every few seconds;
  - hunt mode now scans continuously and averages readings over 0.7 s windows, which gives smoother
    and faster updates.
- **Partition table changed from `huge_app.csv` to `min_spiffs.csv`** (two 1.9 MB app slots for
  OTA). NVS is at the same offset, so settings, Wi-Fi credentials and calibration survive. This
  release must be flashed once over USB.

### Added
- **Over-the-air updates** (ArduinoOTA):
  - The device announces itself as `cyd.local` once Wi-Fi is up.
  - Upload with `pio run -e cyd-ota -t upload`.
  - Password-protected; set it in `[ota]` in `platformio.ini`.
  - Shows a full-screen progress view during the update and reboots when done.
  - The About screen shows the OTA hostname.
- **Wi-Fi analyzer** (**Menu > Wi-Fi analyzer**):
  - Bar graph of networks per channel (1-13), colored by congestion; your channel is in blue.
  - Suggests the best of channels 1/6/11.
  - Lists every AP, including hidden ones, with channel and RSSI.
  - Tap an AP to hunt it.
- **Wi-Fi hunt**: single-channel scans filtered to one BSSID feed the same hunt screen (RSSI,
  trend arrow, bar, history graph).
- **Tracker detector** (**Menu > Tracker detector**):
  - Recognizes Apple Find My offline beacons (AirTags and Find My accessories away from their owner),
    Tile (0xFEED/0xFEEC), Samsung SmartTag (0xFD5A), Google Find My Device (Eddystone frame
    0x40/0x41) and DULT (0xFCB2) trackers.
  - **Watch** mode scans every 30 s and tracks how long and in how many scans each tracker has been
    seen.
  - Trackers seen for 10+ min in 5+ scans turn red, and a one-time warning pops up.
  - Tap a tracker to hunt it.
  - Ignored devices are excluded. Tracker names also show in the BLE scanner.
- Shared hunt module (`src/hunt.h`) with windowed averaging for BLE and Wi-Fi.

## [0.0.9] - 2026-11-18

### Changed
- Status bar order is now temperature, weather icon, clock, Wi-Fi icon.

### Added
- **Weather > Show icon on desktop** switch to hide the forecast icon in the status bar (the
  temperature stays). Saved across reboots; the Weather screen still shows the large icon.

## [0.0.8] - 2026-11-18

### Fixed
- Crash and reboot loop right after each weather update (`assert failed: pbuf_free ... p->ref > 0`).
  In the HTTP helper, `HTTPClient` was declared before the `WiFiClient`/`WiFiClientSecure` it uses.
  Locals are destroyed in reverse order, so `~HTTPClient()` called `stop()` on a client that had
  already been destroyed, which corrupted lwIP's TCP state. The clients are now declared first.
- `Preferences ... NOT_FOUND` errors on first boot. Namespaces are now opened read-write (which
  creates them), and keys are checked with `isKey()` before string reads.

## [0.0.7] - 2026-11-18

### Added
- Weather widget in the status bar, left of the clock: today's forecast icon and the current
  temperature. Tap it for the Weather screen.
- **Menu > Weather**:
  - Large icon and temperature, location name, current conditions, today's forecast with high/low,
    wind (mph or km/h) and chance of rain, and the last update time.
  - **ZIP code** entry with a number keypad.
  - **Units** toggle (Fahrenheit/Celsius) that switches instantly, with no refetch.
  - **Refresh now**.
- Data from Open-Meteo (free, no API key). The ZIP code is resolved once through Zippopotam.us (with
  Open-Meteo geocoding as fallback) and the location is cached in NVS. The forecast refreshes every
  15 min (2 min after a failure). Fetches run in a short-lived background task, so the UI never
  blocks. Plain HTTP is tried first to save the RAM TLS would need, with HTTPS as fallback.
- 9 anti-aliased weather icons (sun, partly cloudy, cloud, fog, drizzle, rain, snow, thunderstorm,
  wind) at 28 and 64 px. They are generated by `tools/gen_wx_icons.py`, with a preview in
  `tools/wx_icons_preview.png`. The wind icon is used when today's max wind is 25 mph or more on an
  otherwise dry day.

### Changed
- Status bar is now a right-aligned row: weather, clock, Wi-Fi. The version label moved out (it is
  still on the About screen and in the serial output). Portrait uses a smaller font so everything fits.
- New dependency: ArduinoJson 7.

## [0.0.6] - 2026-11-18

### Added
- **Brightness** slider under **Menu > Display & touch** (5-100%), saved when you let go. The
  backlight uses 20 kHz PWM, so dimming doesn't add flicker.
- NTP time sync in the background. It starts as soon as Wi-Fi connects (at boot, or later) and syncs
  against `pool.ntp.org` with `1.pool.ntp.org` as fallback. lwIP re-syncs hourly after that.
- Clock in the desktop status bar, next to the Wi-Fi icon. It shows `--:--` until the first sync,
  and tapping it opens Date & time.
- **Menu > Date & time**: large clock with date and sync status, 12/24-hour toggle (default 12-hour),
  time zone picker (22 zones with automatic daylight saving, default US Eastern), and **Sync now**.
  Settings are saved in NVS.

## [0.0.5] - 2026-11-18

### Changed
- The Bluetooth scanner no longer scans when the screen opens. **Start** runs one 8-second scan and
  then stops, and the results stay listed. Tap **Start** again to rescan, which clears the previous
  results. Bluetooth itself is only started the first time you scan.

### Added
- **Ignore** button on a device's detail screen. Ignored devices are hidden from all future scans,
  and the list is saved in NVS across reboots (max 60). "Ignored devices (N)" at the bottom of the
  scan list opens the list: tap an entry to un-ignore it, or **Clear** to remove them all.
- **Hunt** mode from a device's detail screen, to help find a device:
  - Rescans every second and tracks only that device.
  - Shows a large smoothed RSSI readout and a trend arrow: green up = getting closer, red down =
    moving away, grey dash = steady.
  - Shows a signal strength bar and a 40-sample signal history graph.
  - Says "Signal lost" after 4 s without a report.
- LVGL: Montserrat 48 font, bar and chart widgets enabled.

## [0.0.4] - 2026-11-18

### Fixed
- Crash and reboot loop a few seconds after opening the Bluetooth scanner (out of memory):
  - The Arduino BLE library leaks a `BLEAdvertisedDevice` for every repeat advert when
    `wantDuplicates` is on. Duplicates are now off, and the scan restarts every 3 s, so signal
    strength still refreshes.
  - The Bluetooth controller starts in BLE-only mode after Classic Bluetooth's reserved RAM is
    released back to the heap.
  - The scanner copies only the 30 strongest devices per refresh (was the whole list), shows at most
    30 rows (was 60), tracks at most 80 devices (was 150), and frees old rows before building new ones.
  - The list stops adding rows below 20 KB free heap instead of letting LVGL crash on a failed
    allocation.

## [0.0.3] - 2026-11-18

### Added
- Bluetooth LE scanner (**Menu > Bluetooth scanner**): continuous active scan, devices listed
  strongest first with RSSI. Rows show the device name, a manufacturer label (Apple, Samsung, Google,
  etc.) or the address. Tap a device for live details: address type, RSSI, TX power, manufacturer ID,
  service UUID, first and last seen, and advert count. Start/Stop button; scanning stops when you
  leave the screen. Devices drop off after 60 s without an advertisement (max 150 tracked).
- **Display & touch > Swap red/blue** for panels that show blue as orange (saved across reboots).

### Notes
- The ESP32 requires Wi-Fi modem sleep while Bluetooth is active, so opening the scanner turns Wi-Fi
  power save back on until the next reboot.

## [0.0.2] - 2026-11-18

### Fixed
- Desktop Wi-Fi icon stayed grey although the board was connected. The connection tracker only
  recognized a link during its own connect attempt; if the attempt timed out and the Arduino 3.x
  core joined anyway, the state stuck at "failed". The tracker now follows the radio's actual link
  state, and credentials from that attempt are still saved if the late join succeeds.

### Changed
- Connected Wi-Fi icon is now blue (was green).
- Wi-Fi state transitions are logged to serial (`[net] state A -> B`).

## [0.0.1] - 2026-11-18

### Added
- Touch calibration screen (4-point, least-squares fit, rejects sloppy taps). Runs automatically on
  first boot; rerun from **Menu > Display & touch > Calibrate touch screen**, or hold the stylus on the
  screen while resetting the board.
- Calibration is stored in panel-native coordinates, so one calibration works in every orientation.
- Screen orientation setting (landscape, portrait, and both flipped variants) under
  **Menu > Display & touch**, saved across reboots. All screens lay out for either orientation.
- Firmware version shown in the status bar, About screen and on serial at boot.
- About screen shows firmware version and build date; chip revision formatted as vX.Y.

### Fixed
- Screen flicker:
  - ILI9341 panel refresh raised from its ~70 Hz power-on default to 100 Hz.
  - Wi-Fi modem sleep disabled. Its periodic current bursts made the backlight pulse once connected.
  - The UI no longer re-styles the desktop Wi-Fi icon or the Forget button every 300 ms when nothing
    changed, which forced needless redraws.
- SPI clock set to 40 MHz explicitly (the 55 MHz setting already ran at 40 MHz).

### Changed
- Upload speed lowered to 460800 baud (CH340 on macOS was unreliable at 921600).
- LVGL memory and draw buffers allocated from the heap (fixes DRAM overflow on Arduino-ESP32 3.x).

## [Initial import]
- Desktop with Wi-Fi status icon, main menu, Wi-Fi scan / password entry / DHCP connect, saved
  credentials with auto-reconnect.
