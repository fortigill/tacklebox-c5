# Changelog

All notable changes to this project. Versions follow [Semantic Versioning](https://semver.org/);
the version string lives in `src/version.h`.

## [1.7.5] - 2026-11-24

### Changed
- The plane icon behind the status-bar airliner count is larger: 26 px (20 px in portrait),
  was 22 / 16 px. New icons `ui_plane_bar` / `ui_plane_bar_s`.

## [1.7.4] - 2026-11-24

### Changed
- Status-bar airliner count is easier to read: 14 px digits (was 10 px), white on a red
  round badge with a thin white rim, like a phone's notification badge. At zero it's dark
  gray.

## [1.7.3] - 2026-11-24

### Added
- **Status bar airliner count:** a plane icon with the number of airliners within 10 NM of
  the airport as a small badge on its lower right corner (10 px text, like the 5G tag), so
  it's no wider than the icon.
  - Accent color while there's at least one airliner. Gray and dimmed at zero.
  - Tapping it opens the Aircraft near airport page.
  - Switch: "Airliner count in status bar" on that page (on by default). It keeps the live
    tracking running on its own too.
- New 16 px icon `ui_plane_s` for narrow (portrait) layouts.

## [1.7.2] - 2026-11-24

### Changed
- The small menu button that replaces the big Menu button when widgets fill the desktop is
  now a bare 14 px icon in a 22 px slot (was a 40 px button with padding and a background),
  so it fits on the status bar's top line instead of pushing readouts to a second row. Its
  touch area extends 10 px past the icon.

## [1.7.1] - 2026-11-24

### Changed
- Near-the-airport widget, line 2: airline and route ("Allegiant  PIE to BWI") instead of
  airline, callsign and route. The flight number is on line 3 anyway. It reads "route
  unknown" when adsbdb has no route for the callsign. The callsign is still on the page list.

## [1.7.0] - 2026-11-24

### Added
- **Routes for airliners near the airport.** Each airliner's callsign is looked up on
  adsbdb.com, a free public route database (no key). That gives its origin and destination,
  with city names. One-stop routes are handled too.
  - Up to 4 new callsigns are looked up per check. Results are cached for 6 hours (unknown
    callsigns for 1 hour), so a flight is looked up once, not every 30 s.
  - The routes are community data: usually right for scheduled airline flights, but a
    recently changed route can be out of date.
- **Flights to/from DAB widget** (switch on the Aircraft near airport page, on by default).
  It appears **only while** an airliner near the airport is flying to or from it, and hides
  itself otherwise.
  - Line 1: a landing or takeoff plane icon and "To DAB: DL 3154" / "From DAB: AA 5462", in
    the accent color with an accent border.
  - Line 2: the other end and the airline ("from Atlanta (ATL) - Delta"), plus "(1/2)" when
    several take turns every 5 s.
  - Line 3: distance, height, climb / descent and speed, or "On the ground".
- Near-the-airport widget: the route follows the callsign ("Allegiant  AAY2871  PIE-BWI").
- Page list: each airliner gets a route line ("St. Petersburg (PIE) to Baltimore (BWI)").
  Flights to or from this airport are marked "ARRIVING HERE" / "DEPARTED HERE" and shown in
  the accent color.
- The serial log shows each route lookup and how many airliners are flying to or from the
  airport.

## [1.6.5] - 2026-11-24

### Removed
- The takeoff / landing tracker: the "DAB 12 out 11 in" desktop widget, today's counts, the
  last-10 takeoff and landing lists, the flight detail page and the detection logic. Their
  saved settings and counts are erased from flash on the first boot.

### Changed
- The feature is now **Aircraft near airport** (menu). It keeps:
  - **Near-the-airport widget** (its switch is "Show on desktop" on that page): aircraft count
    within 10 NM, then each airliner's airline and callsign, flight number / type / distance,
    and height / climb or descent / speed.
  - **Page:** "20 aircraft near DAB / 1 airliner", the settings (airport code, show on
    desktop, refresh) and the airliners within 10 NM now, with the same details.
  - Same live data (adsb.fi, every 30 s) and the airport lookup.

## [1.6.4] - 2026-11-24

### Changed
- Airline names and callsigns everywhere the flight tracker lists an airliner:
  - **Near-the-airport widget** is now four lines: count; airline and callsign ("Allegiant
    AAY2871", plus "(1/3)" when rotating); flight number, type and distance ("G4 2871  B38M
    7.9 nm"); height, climb or descent and speed.
  - **Airliners near the airport now** (flights page): "Allegiant G4 2871 (AAY2871)", then
    type, phase, distance, height and speed on the second line.
  - **Last 10 takeoffs / landings:** the airline name comes first ("Delta  DL 3154") and the
    callsign starts the second line.

## [1.6.3] - 2026-11-24

### Changed
- The "approaching aircraft" widget is replaced by **Aircraft near the airport** (its own
  switch on the Airport flights page, on by default). It shows what the tracker sees:
  - Line 1: plane icon and how many aircraft of any kind are within 10 NM ("20 near DAB").
  - Line 2: an airliner's flight, type and distance ("G4 2871 B38M  7.9 nm"), plus "(1/3)"
    when there are several. They take turns every 5 s.
  - Line 3: height above the field, climb or descent with an up/down arrow, and ground speed
    ("7191 ft  v1152 fpm  277 kt"), or "On the ground".
  - "No airliners within 10 nm" when there are none. The icon is in the accent color while
    an airliner is nearby.
- "Approaching" now means descending within 10 NM below 12,000 ft (was 6,000 ft), so jets
  starting down toward the field count. This affects the live list's labels and the counts
  widget's "Inbound" line (shown when the "near" widget is off).

## [1.6.2] - 2026-11-24

### Added
- **Approaching aircraft** desktop widget, with its own switch on the Airport flights page
  ("Approaching aircraft on desktop", on by default).
  - Shows the nearest airliner descending toward the airport: flight number and distance
    ("DL 3154  6.2 nm"), then aircraft type, height, speed and a rough time to touchdown
    ("E75L 2400 ft 180 kt, ~2 min").
  - Shows "No inbound to DAB right now" (dimmed) when there isn't one.
  - A nose-down plane icon in the accent color while a flight is inbound. Tapping it opens
    the Airport flights page.
  - While this widget is on, the flights bubble's second line always shows the latest
    takeoff / landing instead of repeating the inbound flight.
  - Either widget being on keeps the live tracking running.
- New icon `ui_plane_land`.

## [1.6.1] - 2026-11-24

### Changed
- Airport flights: every ADS-B check is logged on the serial monitor, not just detected
  takeoffs and landings. Each check prints the reply code, how many aircraft were within
  10 NM, and one line per airliner (flight, callsign, type, distance, height, climb rate,
  speed). A reply without an aircraft list is now reported as an error instead of being
  treated as "no aircraft".

## [1.6.0] - 2026-11-24

### Added
- **Airport flights** (menu, desktop widget): live takeoffs and landings at one airport,
  detected from ADS-B aircraft data (adsb.fi open data, no account needed). Default airport
  is DAB.
  - Every 30 s (60 s on the classic CYD) the board fetches the aircraft within 10 NM of the
    airport. It keeps only airline flights: the callsign must be an airline's (DAL3154,
    JIA5462...), or an unknown operator flying an airliner-sized aircraft. Flight-school,
    private and most business traffic is ignored.
  - **Takeoff:** an airliner within 6 NM, below 3,500 ft above the field and climbing, after
    being on the ground or first seen low.
  - **Landing:** on the ground within 3 NM after being airborne. Or, since receivers often
    can't see the runway at small airports, last seen within 4 NM, under 1,200 ft and
    descending, then gone for 45 s.
  - A plane isn't logged twice within 20 minutes.
  - **Today's counts** (takeoffs / landings since local midnight). They survive a restart on
    the same day.
  - **Last 10 takeoffs and last 10 landings**, each with:
    - the marketed flight number (PSA's JIA5462 reads "AA 5462", Endeavor's EDV5243 reads
      "DL 5243") and the airline, including regional brands like "American Eagle (PSA)";
    - the time and how long ago;
    - the aircraft type (with a friendly name like "Embraer 175" on the detail page), tail
      number, estimated runway (from the heading and the airport's magnetic variation),
      ground speed and height when detected;
    - a FlightAware link to look the flight up.
  - **Airliners near the airport now:** approaching / departing / on the ground / nearby,
    with distance, height and speed, nearest first.
  - **Desktop bubble:** a plane icon with "DAB 12 out 11 in", then an inbound airliner
    ("Inbound: DL 3154, 6 nm") or the latest movement ("AA 5462 took off 4 min ago").
  - **Any airport:** enter a 3-letter code (DAB) or 4-letter ICAO (KDAB). Its position,
    elevation and magnetic variation are looked up once on AviationWeather.gov (NOAA) and
    saved. Changing the airport resets the counts.
  - Airline table covers the U.S. majors and low-cost carriers, regional partners (Endeavor,
    PSA, Envoy, Piedmont, SkyWest, Republic, Mesa...), cargo and the Canadian / Mexican
    carriers.
  - The page credits adsb.fi as its terms require (personal, non-commercial use).
- New icon `ui_plane`.
- On the C5, big JSON downloads parse into PSRAM.

## [1.5.2] - 2026-11-24

### Changed
- Volcano desktop bubble: a small volcano icon (cone and ash plume) replaces the word, and
  the text is shorter: "2 US elevated" / "No US alerts". It no longer runs off the screen.
  The icon takes the color of the most serious U.S. volcano (yellow / orange / red), or the
  normal text color when none is elevated.
- New icon `ui_volcano` in tools/gen_ui_icons.py.

## [1.5.1] - 2026-11-24

### Fixed (NM-CYD-C5)
- **Downloads skipped for "low memory" even though the board has 8 MB of PSRAM.** PSRAM was
  detected but nothing used it, so screens, data lists and HTTPS sessions all came out of the
  ESP32-C5's ~320 KB of internal RAM. After a few screens and an HTTPS session or two, the
  largest free piece fell to ~24 KB, below what a TLS session needs. Stocks, earthquakes and
  volcanoes failed, and internal RAM fell as low as 5 KB during the NWS download. Now:
  - LVGL's widget memory comes from PSRAM. Only the two display draw buffers, which need
    DMA, stay internal.
  - The earthquake list and the weekly volcano report are kept in PSRAM (`BigVec`).
  - mbedTLS allocates its buffers from PSRAM, if this framework build allows changing its
    allocator (checked at compile time; the boot log says "HTTPS buffers in PSRAM"). The
    memory check before each HTTPS download is relaxed to match.
  - Screen updates only pause during HTTPS if internal RAM drops under 24 KB anyway.
- The low-memory message no longer blames Bluetooth on the C5.

### Added
- Boot log and About show internal RAM (free and largest block) and free PSRAM separately.

### Notes
- "MSPI Timing: Failed to allocate dummy cacheline for PSRAM memory barrier!" at power-up
  comes from the ESP-IDF PSRAM driver on this chip revision. PSRAM still works (all 8 MB is
  reported and used).
- The first-boot "nvs_get_blob len fail: eqMin NOT_FOUND" log line is gone.

## [1.5.0] - 2026-11-24

### Added
- **Volcanoes** (menu, a desktop widget and pop-ups).
  - **U.S. volcanoes above normal**, from the USGS Volcano Hazards Program's Hazard
    Notification System (HANS), checked every 15 minutes.
    - Each one shows its aviation color code and volcano alert level (e.g. ORANGE / WATCH),
      color-coded yellow / orange / red, plus the observatory and how long ago the latest
      notice came out.
    - Tap one for the details and the notice's synopsis.
    - Most serious first.
  - **Worldwide this week**, from the Smithsonian / USGS Weekly Volcanic Activity Report
    (RSS), checked every 6 hours since it's published once a week.
    - Each volcano shows its country, whether it's new activity this week or continuing, and
      its distance from your ZIP code.
    - Tap one for that week's report text: about 400 characters on the C5, 100 on the
      classic CYD.
  - **Pop-ups** when a U.S. volcano's level is raised or lowered, joins the elevated list, or
    returns to normal ("View" opens the page). The first check after startup only records
    the levels, so a reboot doesn't pop up everything that's already elevated.
  - **Desktop bubble:** "N US volcanoes elevated" (in orange or red when one is at that
    color), then the most serious one and the number active worldwide.
  - Settings (at the top of the page): show on desktop, pop-ups on level changes, refresh.
  - The weekly report is read with a small streaming tag scanner (CDATA, escaped HTML and
    entities are handled), so it never has to fit in RAM.
- The HTTPS download code is shared by earthquakes and volcanoes, and each source remembers
  whether it redirects to HTTPS so later checks go straight there.

## [1.4.1] - 2026-11-24

### Changed
- Earthquakes, simplified:
  - **Today** (since local midnight) and **yesterday's total** (midnight to midnight). Below
    them: how today compares so far ("Today so far: 40% of yesterday's total", or "Today
    already has 5 more than yesterday").
  - Every quake from the last 24 hours, newest first. The C5 lists up to 200, the classic
    CYD 30, with the full count in the heading.
  - Removed 1.4.0's weekly counts, streaks, 52-week chart and weekly history page. Their
    saved data is erased from flash on the first boot.
  - Yesterday's total is recounted every 10 minutes (one small request), since USGS keeps
    adding late-reviewed quakes for about a day.

## [1.4.0] - 2026-11-24

### Changed
- Earthquakes: the quake list now covers only the last 24 hours (the 48 h / 96 h lists and
  counts are gone), which frees their memory.

### Added
- **Weekly earthquake counts.** Weeks run Monday 00:00 to Sunday 24:00 local time, the same
  for every week, including the ones with a daylight-saving change.
  - "This week" is the running count since Monday.
  - "Last week" shows its total and the change from the week before, as a count and a
    percent (e.g. "up 1 (+1.0%) from 98"), with an arrow.
  - **Streak:** how many weeks in a row the count has gone up (or down), ending last week.
  - **52-week bar chart** on the Earthquakes page, oldest on the left, with the 52-week
    average, high and low.
  - **Weekly history page:** every week of the past year with its count and the change from
    the week before, up in amber and down in green.
  - Counts come from USGS's count service. On first use the 52 past weeks are filled in a few
    at a time, every 10 minutes (8 per round on the C5, 3 on the classic CYD). Last week is
    recounted each round, because USGS keeps adding late-reviewed quakes for a few days.
  - The history is saved in flash and survives restarts. Changing the minimum magnitude or
    area (or the ZIP code while using "within") starts it over, since the old counts used a
    different filter.
- The C5 no longer pauses screen updates during HTTPS downloads. That pause is only needed
  for the classic CYD's small memory.

## [1.3.1] - 2026-11-24

### Changed
- Earthquakes page: the settings (minimum magnitude, area, show on desktop, refresh) now sit
  right under the summary, above the 24 h / 48 h / 96 h buttons and the quake list. No more
  scrolling past every quake to reach them.

## [1.3.0] - 2026-11-24

### Added
- **Earthquakes** (menu, and a desktop widget), from the USGS Earthquake Hazards Program's
  official catalog (FDSN event service). Refreshed every 10 minutes.
  - Counts for today (since local midnight) and the last 24, 48 and 96 hours.
  - The largest quake of the last 24 hours.
  - A list of quakes for a 24 h / 48 h / 96 h window, newest first. Each shows the magnitude
    (color-coded: green under 3, amber 3+, orange 4.5+, red 6+), place, how long ago, depth
    and distance from your ZIP code. Quakes from the last hour are highlighted.
  - Tap a quake for its official readings: local time, magnitude and magnitude type, depth in
    km and mi, coordinates, distance, reporting network and USGS event ID.
  - Settings: minimum magnitude (1.0 / 2.5 / 4.5 / 6.0, default 2.5) and area (worldwide, or
    within about 1000 / 500 / 250 / 100 mi of the Weather ZIP code).
  - Desktop bubble: "N quakes today". Below it is the newest quake while it's under an hour
    old, otherwise the largest of the last 24 h.
  - The data comes as plain text and is read line by line off the connection, so even
    thousands of small quakes need only a small buffer. The list keeps up to 250 quakes on the
    C5 and 30 on the classic CYD (counts are always complete).
  - Like the other downloads, it pauses while Bluetooth tools or the drone scanner run.

## [1.2.4] - 2026-11-24

### Added
- Buoy picker: 12 Gulf buoys that report waves, grouped under regional headings.
  - Florida Gulf coast: 42097 Pulley Ridge, 42099 Offshore St. Petersburg, 42098 Egmont
    Channel, 42036 West Tampa.
  - Panhandle & Alabama: 42039 Pensacola 115 NM SSE, 42012 Orange Beach, 42040 Dauphin Island
    64 NM S (Luke Offshore).
  - Central & western Gulf: 42001 Mid Gulf, 42002 West Gulf, 42035 Galveston, 42019 Freeport,
    42020 Corpus Christi.
  - Tide gauges and met-only stations (Panama City, Apalachicola, Pensacola pier, the USF COMPS
    moorings) are left out because they don't measure waves.
- The existing Atlantic buoys are now under "Florida Atlantic coast" and Satan Shoal under
  "Florida Keys". 41012 moved next to St. Augustine.
- A favorite that's now built in shows once, in its region.

## [1.2.3] - 2026-11-24

### Changed
- Status bar clock: AM/PM is now small 10 px text on the time's baseline, the same size as the
  5G tag, so it takes much less room. It's still controlled by **AM/PM in status bar**. Both
  boards now include the 10 px font (about 5-8 KB of flash on the classic CYD).

## [1.2.2] - 2026-11-24

### Changed
- The green 5G tag on the Wi-Fi icon is about 25% smaller: 10 px text instead of 14 px, with
  slimmer padding. The 10 px font is only built into the C5 firmware.

## [1.2.1] - 2026-11-24

### Added
- Status bar: while connected on 5 GHz, the Wi-Fi icon gets a small green **5G** tag on its
  lower right. On 2.4 GHz it stays the plain blue icon. The tag also updates when the board
  switches bands. Dual-band boards only.

## [1.2.0] - 2026-11-24

### Added (NM-CYD-C5 / dual-band boards)
- **5 GHz Wi-Fi.** The radio runs in 2.4 + 5 GHz mode, so scans, connections and the
  analyzer cover both bands.
- **Prefers 5 GHz, falls back to 2.4 GHz.** When joining a network, Tackle Box picks that
  network's 5 GHz access point if it's at least -72 dBm, otherwise the strongest one on either
  band. While connected:
  - on 5 GHz, if the signal averages below -80 dBm for 15 s, it looks for a clearly stronger
    access point (5 dB or more) and moves to it, normally the same router's 2.4 GHz side;
  - on 2.4 GHz, it checks every 3 minutes whether 5 GHz is back to -67 dBm or better and
    moves up if so;
  - if the connection drops, it rescans both bands after 8 s (then every 30 s) and joins
    whatever is best, instead of retrying only the access point it lost.
  The up and down thresholds are far apart, so it doesn't flip back and forth. Each decision
  is logged on the serial monitor ("[net] 5 GHz signal weak: joining ...").
- **Wi-Fi analyzer:** a 2.4G / 5G switch in the header shows each band on its own: its own
  network list, and its own congestion graph. On 5 GHz each bar is one 80 MHz block (36, 52,
  100, 116, 132, 149, 165), and the best-channel tip picks the 36 or 149 block, which need no
  radar check. The status line counts both bands. Hunting a 5 GHz access point works as on
  2.4 GHz.
- **Wi-Fi list:** each network shows the bands it's on (2.4G, 5G or 2.4+5G).
- **Wi-Fi status and About** show the band and channel of the current connection.

### Unchanged
- The classic CYD (2.4 GHz only) behaves exactly as before.

## [1.1.0] - 2026-11-24

### Added
- Support for the **RockBase NM-CYD-C5** (ESP32-C5, single-core RISC-V, dual-band Wi-Fi 6,
  BLE 5, 16 MB flash, 8 MB PSRAM, ST7789 2.8" 240x320, XPT2046 resistive touch, WS2812 LED).
  This is a first test build.
  - New build targets `cyd-c5` (USB) and `cyd-c5-ota` (Wi-Fi). The classic board keeps `cyd` /
    `cyd-ota`.
  - `src/board.h`: per-board pins and defaults. The C5 pins come from RockBase's wiki and the CYM
    project: SPI SCK 6, MOSI 7, MISO 2 (shared), LCD CS 23 / DC 24 / no reset, backlight 25,
    touch CS 1 (polled), SD CS 10 (held high), WS2812 on 27.
  - `src/display_c5.cpp`: ST7789 through ESP-IDF's esp_lcd (the C5 is officially supported
    there, unlike TFT_eSPI), with asynchronous DMA flushing and two 20-line buffers. The XPT2046
    is a second SPI device on the same bus, read only while no pixel transfer is in flight
    (median of 3 samples, Z1/Z2 pressure). It exposes the same `display.h` API, so calibration,
    rotation, brightness, invert and red/blue swap work unchanged.
  - LVGL byte-swaps RGB565 on the C5 build (`LV_COLOR_16_SWAP`), since esp_lcd sends the buffer
    as-is.
  - `partitions_c5_16mb.csv`: two 6 MB OTA slots, 1 MB SPIFFS and a core dump.
    `tools/check_size.py` now reads the slot size from each build's partition table.
  - The C5's network name is `tacklebox-c5.local`, so both boards can share a network.
  - The WS2812 status LED is driven through `rgbLedWrite` at a dim level.
  - The About screen shows the board, single/dual core and PSRAM. CPU load reports one figure on
    single-core chips. The boot banner names the board.
- The default for "Invert colors" is now per board: on for the classic CYD, off for the C5.

### Not yet verified on the C5
- Bluetooth tools, the Wi-Fi analyzer and Drone Remote ID compile, but they're untested on the
  C5. 5 GHz Remote ID (channel 149) comes in the next step.

## [1.0.0] - 2026-11-19

The project is now officially **SaltyGills Tackle Box**: *lots of handy tools in one small box.*

### Changed
- The name is used everywhere: the boot banner on serial ("SaltyGills Tackle Box v1.0.0" plus
  the slogan), the About screen (name and slogan on top), the status bar label in landscape
  ("Tackle Box"), the README, and the HTTP User-Agent (`SaltyGillsTackleBox/1.0.0`).
- Network name: the device answers as **`tacklebox.local`** (was `cyd.local`) and sets its DHCP
  hostname to `tacklebox`, so it shows up by name in the router's client list.
  `platformio.ini` `[ota] hostname` is now `tacklebox.local`. The first upload of 1.0.0 still
  has to go to the old name or IP (or over USB), because the board answers as `cyd.local` until
  it runs 1.0.0.
- New `version.h` constants: `PRODUCT_NAME`, `PRODUCT_SHORT`, `PRODUCT_TAGLINE`,
  `PRODUCT_HOSTNAME`, `PRODUCT_UA`.

### What's in the box (1.0.0)
- Desktop with a status bar (stocks, NFL, waves, weather, drone icon, clock, Wi-Fi) that wraps to
  a second row when it fills up, plus tappable widgets for waves, NWS alert count, stocks and
  NFL scores.
- Weather (Open-Meteo) with NWS alerts, pop-ups and snooze. NOAA NDBC buoys with favorites.
  Stocks (up to 8 tickers). NFL scores for your team.
- Drone Remote ID scan/watch (Wi-Fi beacon, NaN, BLE; DJI confirmed), BLE scanner with hunt,
  tracker detector, Wi-Fi analyzer.
- NTP clock (12/24 h, optional AM/PM), themes, rotation, brightness, calibration, OTA with
  on-device approval, uptime and CPU load on the About screen, restart button.
- Network downloads pause while Bluetooth tools run, and Bluetooth releases its memory 1 min
  after the last tool stops.

## [0.9.3] - 2026-11-19

### Changed
- The desktop NFL bubble has two lines (score and ">" on top, game clock / kickoff time / "Final"
  below) instead of one wide line that was cut off at both screen edges. A detail that's still
  too long ends in "...", and the second line hides when there's nothing to show (e.g.
  "NFL: pick a team").

### Confirmed
- 0.9.2 reads the ESPN scoreboard (tonight's game shows).

## [0.9.2] - 2026-11-19

### Fixed
- NFL scores failing with "bad scoreboard data: TooDeep". ArduinoJson's default nesting limit is
  10, and it applies to the parts the filter skips too. ESPN's scoreboard nests player leaders
  and links deeper than that. The scoreboard is now parsed with `NestingLimit(32)`.

### Confirmed
- 0.9.1 builds with 57 KB free in the OTA slot (NimBLE observer-only saved about 70 KB).

## [0.9.1] - 2026-11-19

### Fixed
- 0.9.0 didn't build: firmware.bin was 1,978,944 bytes, 13 KB over the 1,966,080-byte OTA slot.
  NimBLE is now built without the client (central), server (peripheral) and advertising
  (broadcaster) roles (`CONFIG_BT_NIMBLE_ROLE_*_DISABLED`). The app only scans (observer role),
  so nothing changes in use. NimBLE's docs put the saving at roughly 7 + 16 + 5 KB.
  Includes all the 0.9.0 changes.
- OTA uploads weren't using the patched espota.py, so the device had about 10 s instead of 45 s
  to be approved. PlatformIO switches any host-name `upload_port` back to `espota`, which
  overrode `upload_protocol = custom`. `tools/espota_patch.py` now runs as a post script and
  points the platform's espota uploader (`$UPLOADER`) at `.pio/espota_cyd.py`.

## [0.9.0] - 2026-11-19

### Added
- NFL scores:
  - **Menu > NFL scores**: pick a team (all 32 teams, or none).
  - **Status bar**: your team's game, "SEA 21-17 SF" (away team first, as ESPN lists it), in the
    accent color while live, with " F" when final. It shows only when there's a game.
  - **Desktop**: a one-line bubble with the score and the game clock, kickoff time or "Final".
  - **NFL page**: a large score, teams, clock, down and distance and possession while live, and all
    of this week's games (live games and yours highlighted).
  - Two switches: **Score in status bar** and **Score on desktop**.
- How it's fetched: ESPN's public scoreboard (`site.api.espn.com/.../football/nfl/scoreboard`,
  no key), plain HTTP first and HTTPS only if redirected.
  - The reply is hundreds of KB, so it's parsed straight off the connection by `BodyStream`
    (chunked/Content-Length decoding with a 15 s stall limit) with an ArduinoJson filter that
    keeps only teams, scores, status and the situation, and never stored.
  - Refreshes every 60 s while your team's game is on, every 2 min in the 30 min before kickoff,
    and every 30 min otherwise. The first check is 75 s after boot. It pauses while Bluetooth tools
    are in use.
- Desktop space handling: when the widgets need the room, the Menu button shrinks (to 40 px),
  then the Wi-Fi line at the bottom hides, and finally the Menu button is replaced by a small menu
  icon at the left of the status bar.

## [0.8.3] - 2026-11-19

### Changed
- The desktop alert bubble reads "5 NWS alerts" / "No NWS alerts", followed by the warning icon.

## [0.8.2] - 2026-11-19

### Added
- About screen: live **CPU load** per core ("CPU load: 8% radio core, 23% app core"), refreshed
  every second. The line turns amber at 70% and red at 90%. Core 0 runs Wi-Fi and Bluetooth;
  core 1 runs the UI and app loop.
- `src/sysmon.*`: FreeRTOS idle hooks on both cores count the 1 ms ticks in which the idle task
  ran. Load is the share of ticks without idle time over the last second. The hooks return true,
  so the cores still sleep between interrupts. The figure reads slightly low (a mostly busy tick
  with a brief idle counts as idle), which is fine for spotting trends.

## [0.8.1] - 2026-11-19

### Added
- Multiple stock tickers, up to 8:
  - **Menu > Stocks** lists them all with price and day change. **Add ticker** adds one, and
    tapping a ticker opens its detail page, which has **Remove ticker**.
  - The status bar and desktop readouts rotate through the tickers every 5 s.
  - The 0.8.0 single ticker carries over as the first entry.
- All tickers are fetched over one TLS session: HTTPClient keep-alive reuses the connection, so
  there's a single handshake per update. Each reply goes to its own flash file and is parsed after
  the connection closes. A failed ticker shows its own error and keeps its last good price.

### Fixed
- `streamBody()` now consumes the end of a chunked reply (the final CRLF and any trailers), so a
  kept-alive connection starts the next response cleanly. This was host-tested with chunked,
  chunked-with-trailer and Content-Length bodies.

## [0.8.0] - 2026-11-19

### Added
- Stock tracker:
  - **Menu > Stocks**: pick a ticker on the on-screen keyboard (any NYSE/NASDAQ stock, ETF or
    index Yahoo Finance knows: AAPL, TSLA, BRK-B, ^GSPC...). The default is SPY.
  - **Status bar**: ticker, price and the day's change in %, green when up and red when down,
    left of the wave reading.
  - **Desktop**: a one-line bubble (ticker, price, change, ">"). Tap it, or the status bar reading,
    for the stock page: change in $ and %, previous close, day range, exchange, currency and the
    last trade time.
  - Two switches, **Stock in status bar** and **Stock on desktop** (both, either or neither). The
    quote is only fetched while at least one is on.
- How it's fetched: Yahoo Finance's chart endpoint
  (`query1.finance.yahoo.com/v8/finance/chart/<SYM>?range=1d&interval=1d`, no key) over HTTPS.
  The reply goes to flash, then gets a filtered parse (same path as the NWS alerts). The day
  change is computed from `regularMarketPrice` against the previous close.
  - Refreshes every 5 min on weekdays 09:00-16:30 Eastern (DST handled) and every 30 min
    otherwise; retries after 2 min. The first quote comes 60 s after boot.
  - Like the weather and buoy, it pauses while Drone Scan, Drone Watch or a Bluetooth tool is in
    use, and it never runs alongside another download.

### Changed
- Desktop layout: in landscape the bubbles wrap onto a second line. The Menu button now
  shrinks (down to 40 px tall) instead of overlapping the Wi-Fi line when the widgets need the
  room.

## [0.7.16] - 2026-11-19

### Added
- About screen:
  - A live **uptime** counter ("Uptime: 1 d 03:14:07"). It uses the 64-bit `esp_timer`, so it
    doesn't wrap after 49 days like `millis()`.
  - A **Restart device** button that asks for confirmation, shows "Restarting..." and then
    soft-reboots (`ESP.restart()`). Settings are kept. The next boot logs
    `reset reason: software restart`.

## [0.7.15] - 2026-11-19

### Added
- **Date & time > AM/PM in status bar**: in 12-hour mode the status bar clock can show just
  "2:47" instead of "2:47 PM". It's on by default and saved in NVS. The Date & time page itself
  still shows AM/PM, and the switch has no effect in 24-hour mode.

## [0.7.14] - 2026-11-19

### Fixed
- NWS alerts failing with "download failed: too less ram after 0 KB". `HTTPClient::writeToStream()`
  allocates its copy buffer after the TLS session has fragmented the heap (the largest block was
  down to about 7 KB). Alerts are now copied to flash by `streamBody()`, which uses a 1 KB buffer
  reserved at boot and decodes chunked transfer encoding itself, so nothing is allocated mid-download.

### Confirmed
- 0.7.13's pause: no weather or buoy downloads while Drone Watch runs, and the buoy updates
  right after Bluetooth is released.

## [0.7.13] - 2026-11-19

### Changed
- Weather, NWS alert and buoy downloads pause while Drone Scan, Drone Watch or any Bluetooth tool
  is in use, and until Bluetooth is released 1 min after the tool stops. Overdue updates run
  right after that. The alert list and buoy page say "Paused while the drone scanner or a
  Bluetooth tool is in use" meanwhile. This avoids the failed "low memory" attempts seen when
  HTTPS and the Bluetooth stack competed for heap.

### Confirmed
- 0.7.11/0.7.12 Bluetooth release: free heap goes from about 45 KB back to about 95 KB a minute
  after the tool stops, and the next buoy download succeeds.

## [0.7.12] - 2026-11-19

### Fixed
- 0.7.11 didn't build: firmware.bin was 1,967,968 bytes, 2 KB over the 1,966,080-byte OTA slot
  (`tools/check_size.py` stopped it). `lv_conf.h` now turns off LVGL widgets and features the
  app never uses, which LVGL enables by default: line, table, checkbox, dropdown, roller,
  canvas, animimg, calendar, colorwheel, imgbtn, LED, menu, meter, span, spinbox, tabview,
  tileview, win, the basic/mono themes, grid layout and snapshot. Nothing visible changes.
  Includes all the 0.7.11 changes.

## [0.7.11] - 2026-11-19

### Fixed
- Buoy data (and NWS alerts) failing after Drone Watch or other Bluetooth tools had run:
  "X509 - Allocation of memory failed". NDBC redirects plain HTTP to HTTPS, and NimBLE kept about
  20 KB of heap until reboot, which left ~45 KB free with a ~37 KB largest block, too little for
  TLS.
  - Bluetooth is now shut down (`NimBLEDevice::deinit`) once no tool has used it for 60 s, which
    returns its memory. It starts again automatically the next time a Bluetooth tool, Drone Scan
    or Drone Watch needs it. A running Drone Watch keeps it (its rests are 25 s).
  - The HTTPS memory check also requires 54 KB of total free heap, not just a 36 KB block, so a
    download that can't succeed is skipped with a clear message instead of a TLS error.

### Confirmed
- 0.7.10's radio time-sharing fix: Drone Watch now detects a DJI Avata 2 over Wi-Fi beacon
  Remote ID.

## [0.7.10] - 2026-11-19

### Fixed
- Drone Scan and Watch missed DJI drones (tested with a DJI Avata 2 that another ESP32 firmware
  on the same board detected immediately). DJI sends Remote ID only in Wi-Fi beacons, but the
  Bluetooth Remote ID scan ran at 99% duty (100 ms interval, 99 ms window) on the single 2.4 GHz
  radio the ESP32 shares between Wi-Fi and Bluetooth, which left the Wi-Fi sniffer almost deaf.
  The two now take turns: 2.4 s of Wi-Fi only, then 0.8 s of Bluetooth. While hunting a Bluetooth
  transmitter, Bluetooth stays on.
- Channel dwell went from 110 ms to 150 ms, so beacons on the channels other than channel 6
  (5 Hz there per ASTM F3411) are caught more reliably.

### Added
- While scanning, a serial log line every 10 s: beacons heard, how many carried Remote ID, NaN
  frames, and transmitters found.

## [0.7.9] - 2026-11-19

### Fixed
- Reboots and NWS alerts stopping since 0.7.x. The new desktop widgets and buoy data use more
  heap, which left even less free while an HTTPS download (NWS alerts, or NDBC if it redirects
  to HTTPS) was running:
  - The 300 ms UI poll now pauses while a TLS session is open. Label updates and pop-ups
    allocate from the same heap, and a failed LVGL allocation during the download could crash
    the board. Taps are still handled, and updates resume when the download ends (a few seconds).
  - The desktop rebuilds the wave texts only when the buoy data, station or unit changes, instead
    of copying Strings every 300 ms (heap churn and fragmentation, which made alert checks get
    skipped with "low memory").
  - The LVGL draw buffer is 16 lines instead of 24, which frees 5 KB of heap.

### Added
- The boot log prints the reset reason (power on, OTA/software restart, crash/panic,
  watchdog, brownout), so a reboot can be diagnosed.
- The buoy log shows free heap and the largest block for each request.

## [0.7.8] - 2026-11-19

### Changed
- The clock and Wi-Fi icon always stay on the top row of the status bar. Only the other items
  (waves, temperature and weather icon, drone icon) move to the second row when space runs out.
  The bar's left part wraps on its own, and a 32 px strut keeps its first line aligned with the
  clock and the Wi-Fi button.

## [0.7.7] - 2026-11-19

### Changed
- The status bar wraps instead of running off the screen edge. When its items (waves,
  temperature, weather icon, drone icon, clock, Wi-Fi) don't fit on one line, it grows by just
  enough for a second row. The desktop widgets and the Menu button move down to match, and they
  go back when it fits again (for example after Drone Watch stops).

## [0.7.6] - 2026-11-19

### Changed
- The value now comes before the icon: wave height then wave icon in the status bar and the
  desktop widget, and "5 alerts" / "No alerts" then the warning icon in the alert widget.

## [0.7.5] - 2026-11-19

### Changed
- The desktop wave bubble shows the buoy's friendly name above the wave icon and height (for
  example "Canaveral 20 NM East"). Typed-in stations show as "Station 42078". Long names end in
  "...".
- The built-in buoy names are shorter so they fit the widget.
- The Menu button sits a little lower to make room for the taller wave bubble.

## [0.7.4] - 2026-11-19

### Changed
- **Choose buoy** now lists, in this order: 41112 Offshore Fernandina Beach, 41117 St.
  Augustine, 41070 Ponce de Leon Inlet, 41009 Canaveral 20 NM E (still the default), 41012 St.
  Augustine 40 NM ENE, 41113 Cape Canaveral Nearshore, 41114 Fort Pierce, 41122 Hollywood Beach
  and 42095 Satan Shoal (Key West). 41010 Canaveral East was dropped from the list, but it can
  still be typed in.

### Added
- Buoy favorites: a station entered under **Other station ID...** is added to a Favorites
  section at the top of the list (newest first, up to 8, saved across reboots). Built-in buoys
  aren't duplicated. Hold a favorite to remove it.

## [0.7.3] - 2026-11-19

### Fixed
- The desktop wave and alert bubbles were cut off at the screen edges in portrait. They now
  stack on top of each other in portrait (the Menu button moves down to make room) and stay
  side by side on one line in landscape, with slightly tighter padding.

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
