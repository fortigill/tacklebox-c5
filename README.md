# SaltyGills Tackle Box

*Lots of handy tools in one small box.*

A stylus-driven LVGL 8.3 touch UI for the ESP32-2432S028R "Cheap Yellow Display" (2.8"). It packs
weather with NWS alerts, NOAA buoy data, stocks, NFL scores, a drone Remote ID watcher, Bluetooth
and Wi-Fi tools, and over-the-air updates into one small box. Current version: see `src/version.h`
and [CHANGELOG.md](CHANGELOG.md).

## Supported boards

| Board | Build | Notes |
|---|---|---|
| Classic CYD, ESP32-2432S028R | `pio run -e cyd -t upload` (USB), `-e cyd-ota` (Wi-Fi, `tacklebox.local`) | ESP32, ILI9341, 4 MB flash |
| RockBase NM-CYD-C5 | `pio run -e cyd-c5 -t upload` (USB), `-e cyd-c5-ota` (Wi-Fi, `tacklebox-c5.local`) | ESP32-C5 (single core, dual-band Wi-Fi 6), ST7789, 16 MB flash + 8 MB PSRAM |

Both boards share one codebase. The hardware differences (pins, display/touch driver, status LED)
live in `src/board.h`, `src/display.cpp` (classic: TFT_eSPI) and `src/display_c5.cpp` (C5:
ESP-IDF esp_lcd with the XPT2046 on the same SPI bus). On first boot the C5 asks for a touch
calibration, like a new classic board. If the colors look wrong, use **Display & touch > Invert
colors** or **Swap red/blue**.

- **Desktop**: status bar with firmware version and a Wi-Fi icon (blue = connected, amber =
  connecting, dim grey = offline). Tap the icon for Wi-Fi settings, or tap **Menu**.
- **Menu**: Network settings, Drone Remote ID, Bluetooth scanner, Tracker detector, Wi-Fi analyzer,
  Weather, Stocks, NFL scores, Date & time, Display & touch, About this device.
- **Drone Remote ID**: listens for standard Remote ID (including DJI's built-in Remote ID) on Wi-Fi
  beacons, Wi-Fi NaN and Bluetooth 4.
  - **Scan** runs nonstop until you tap **Stop**, with live ID, position, altitude, speed, operator
    location/distance and a signal hunt. Wi-Fi is paused while it runs.
  - **Watch** scans 5 s every 30 s (longer while a drone is in range), with Wi-Fi back in between, and keeps a **log** of every drone
    seen (saved until you delete it). While it runs, a drone icon sits in the status bar and flashes
    blue/red when a drone is near. Each drone triggers a pop-up with **Close** or **View**.
- **Tracker detector**: **Watch** scans every 30 s for AirTag/Find My, Tile, SmartTag and Google
  Find My trackers. One that stays with you for 10+ min turns red and triggers a warning. Tap it to
  hunt it.
- **Wi-Fi analyzer**: networks-per-channel graph, best-channel hint, and every AP with its channel
  and RSSI. Tap one to hunt it.
- **Weather**: set a ZIP code under **Menu > Weather**. The status bar then shows today's forecast
  icon and the current temperature (F or C). Data comes from Open-Meteo (no API key) and refreshes
  every 15 min. Regenerate the icons with `python3 tools/gen_wx_icons.py`.
- **NWS alerts**: active National Weather Service alerts for the weather ZIP, checked every 5 min.
  New alerts pop up and make the desktop weather icon flash until snoozed. **Weather > Active
  alerts** lists them with full details until they expire.
- **Buoy data**: the wave height from one NOAA NDBC buoy (feet and inches, or meters in Celsius
  mode), shown with a wave icon in the status bar (left of the weather) and as a larger tappable bubble on the
  desktop. Each has its own switch on the buoy page, so you can have both, either or neither. With
  both off the buoy isn't polled. The default buoy is 41009, 20 NM east of Cape Canaveral. Tap
  either reading, or **Weather > Buoy data**, for the full observation: wave direction and periods,
  wind and gusts in knots, water and air temperature, pressure and observation time. **Choose buoy**
  lists 41112, 41117, 41070, 41009, 41012, 41113, 41114, 41122 and 42095. A station typed in under
  **Other station ID** is saved as a favorite at the top of the list (hold a favorite to remove it). Data refreshes every 30 min.
- **Alert count**: a desktop readout next to the wave height shows how many NWS alerts are
  active (red while any are unsnoozed). Tap it for the alert list. Switch it off under **Weather >
  Alert count on desktop**.
- **Stocks**: track up to 8 tickers (any NYSE/NASDAQ stock, ETF, index or future Yahoo Finance
  knows, e.g. AAPL, BRK-B, ^GSPC, GC=F for gold). The status bar and a desktop bubble show the price
  and the day's change (green up, red down), rotating through your tickers every 5 s. Each has its
  own switch under **Menu > Stocks**. That page lists every ticker with its price and change. Tap
  one for details (change in $ and %, previous close, day range, exchange, last trade) or to
  remove it, and use **Add ticker** for more. Quotes refresh every 5 min while the US market is open
  and every 30 min otherwise, all over one HTTPS connection. Like the weather and buoy, updates
  pause while Drone Scan, Drone Watch or a Bluetooth tool is in use. Data comes from Yahoo Finance's
  free chart endpoint (unofficial, may be delayed).
- **NFL scores**: pick your team under **Menu > NFL scores**. Its game this week shows in the
  status bar ("SEA 21-17 SF", accent color while live) and in a desktop bubble with the game clock
  or kickoff time, each with its own switch. The NFL page adds down and distance and who has the
  ball, plus every game of the week. Scores refresh every minute while your team plays, every
  2 min in the half hour before kickoff, and every 30 min otherwise. Like the other downloads,
  updates pause while Drone Scan, Drone Watch or a Bluetooth tool is in use. Data comes from ESPN's
  public scoreboard (unofficial). It's fetched over plain HTTP when possible and parsed as it
  streams in, keeping only teams, scores and the clock.
- **Desktop space**: with many widgets on, the Menu button shrinks, then the Wi-Fi line at the
  bottom hides, and finally the Menu button gives way to a small menu icon at the left of the
  status bar.
- **Clock**: synced from `pool.ntp.org` in the background once Wi-Fi is up. It shows in the status
  bar, 12-hour by default. **Date & time** sets the time zone and 12/24-hour format, or forces a sync.
- **Wi-Fi**: async scan, networks sorted by signal, on-screen keyboard for the password, DHCP connect,
  credentials saved in NVS with auto-reconnect on boot, **Forget** to clear them. Note: the password
  is stored unencrypted in flash.
- **Bluetooth scanner**: tap **Start** for an 8-second scan of nearby BLE devices. Tap a device for
  details, **Ignore** it (hidden from future scans, saved), or **Hunt** it. Hunt shows a live signal
  readout, a closer/farther arrow and a history graph to help you find it. Phones that use rotating
  random addresses can't be ignored permanently, and a hunt loses them when the address changes.
- **Display & touch**: theme (Dark, Light, Miami Vice, Cyber Hacker), brightness, landscape/portrait (plus flipped variants), red/blue swap for panels that show
  blue as orange, and touch calibration.

## Board

Confirmed: ESP32-D0WD-V3 rev 3.1, 4 MB flash (Boya), CH340 USB-serial on `/dev/cu.usbserial-3110`.

| Function | Pins |
|---|---|
| TFT ILI9341 (HSPI) | MOSI 13, MISO 12, SCLK 14, CS 15, DC 2, BL 21 |
| Touch XPT2046 (VSPI) | CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36 |
| SD card | CS 5, MOSI 23, MISO 19, SCK 18 |
| RGB LED (active low) | R 4, G 16, B 17 |
| LDR / Speaker | 34 / 26 |

## Build and flash

PlatformIO must run on Python 3.10–3.14. Install it with `pipx install --python python3.13 platformio`.

```bash
# USB
pio run -t upload --upload-port /dev/cu.usbserial-3110
pio device monitor -p /dev/cu.usbserial-3110

# Over Wi-Fi (OTA): the device must be on the network
pio run -e cyd-ota -t upload
```

OTA uploads go to `tacklebox.local` with the password in the `[ota]` section of `platformio.ini`. Change
the password, then flash once over USB so the device knows it. If mDNS doesn't resolve, set
`hostname` in `[ota]` to the device's IP (it's shown on the About screen). A change to the partition
table (as in 0.1.0) always needs a USB flash.

Every OTA push has to be approved on the device. A "Firmware update" pop-up offers **Accept**
or **Decline**, with a 30-second countdown. If nobody taps Accept, the update is blocked, the
upload fails on the computer, and the current firmware keeps running. USB flashing isn't affected.
Stock espota.py gives up after 10 s, so `pio run -e cyd-ota` uses a copy with a 45 s wait.
`tools/espota_patch.py` generates it in `.pio/` at build time.

## Touch calibration

- Runs automatically the first time the firmware boots.
- Rerun it from **Menu > Display & touch > Calibrate touch screen**.
- If touch is so far off you can't reach the menu, **hold the stylus on the screen and press reset**.
- Tap the center of each of the 4 crosses. If the taps don't line up within 20 px, it asks you to
  try again.
- The result is saved in panel-native coordinates, so it stays valid when you rotate the screen.

## Versioning

The project is a git repository.

1. Bump `FW_VERSION` in `src/version.h`.
2. Add a `CHANGELOG.md` entry.
3. Commit, then tag the release: `git tag -a v0.0.2 -m "v0.0.2"`.

`git log --oneline --decorate` shows the history.

## Troubleshooting

- **Upload fails after "Changing baud rate"**: lower `upload_speed` in `platformio.ini` to 115200.
- **Colors look inverted** (dark looks light, blue looks orange): toggle **Display & touch > Invert
  colors**. It is on by default because this board's panel needs it.
- **Red and blue swapped** (but light/dark correct): toggle **Swap red/blue**.
- **Colors still wrong**: some CYDs, including the two-USB-port version, use an ST7789 panel and need
  a different driver flag.

## Layout

```
platformio.ini      board, libraries, TFT_eSPI pin config
include/lv_conf.h   LVGL config
src/version.h       firmware version
src/main.cpp        setup/loop
src/display.cpp     TFT + touch drivers, rotation, calibration
src/theme.cpp       color themes (child theme over LVGL default)
src/drone.cpp       drone Remote ID: Wi-Fi sniffer, ODID decoder
src/ble.cpp         BLE scanner, trackers, hunt, Remote ID adverts (NimBLE)
src/hunt.h          shared RSSI averaging for hunt modes
src/ota.cpp         over-the-air updates (ArduinoOTA)
src/sysmon.cpp      CPU load per core (idle hooks)
src/led.cpp         rear RGB LED (boot status: red, blinking red, green)
src/weather.cpp     weather fetch (Open-Meteo), ZIP lookup, icon mapping
src/wx_icons.c      generated weather icons (tools/gen_wx_icons.py)
src/timesvc.cpp     NTP sync, time zone, 12/24 h formatting
src/net.cpp         Wi-Fi scan/connect/save (non-blocking state machine)
src/ui.cpp          screens
```
