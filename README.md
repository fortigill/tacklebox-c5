# SaltyGills Tackle Box

*Lots of handy tools in one small box.*

A touch-screen dashboard and pocket toolkit for the **RockBase NM-CYD-C5** (ESP32-C5, 2.8"
240×320 touch display). It puts weather and NWS alerts, buoy wave heights, stocks, NFL scores,
earthquakes, volcanoes, aircraft near your airport, breaking news, unread email, service outages,
a drone Remote ID watcher, Bluetooth and Wi-Fi tools on one small screen. Everything updates on
its own over Wi-Fi, and you can pick which widgets show on the desktop.

**Current release: v1.13.2.** See [CHANGELOG.md](CHANGELOG.md) for every change.

- **Flash it:** the easiest way is the **[FortiGill Web Flasher](https://www.fortigill.net/flasher.html)**.
  Open it in Google Chrome, plug in the board, and click through three steps; it always offers
  the latest release. [FLASHING.md](FLASHING.md) covers that and the other ways (esptool,
  building from source) for Windows, macOS and Linux.
- **Print a case:** a snap-together case with a built-in stylus holder is included. See
  [3D-printed case](#3d-printed-case).

---

## Features

### Desktop

- **Status bar:** clock, Wi-Fi icon (a small green **5G** badge when connected on 5 GHz), weather
  icon and temperature, plus optional live items: buoy wave height, stock ticker, NFL score,
  aircraft near your airport (plane with a count badge) and unread email (envelope with a count
  badge).
- **Widgets:** tappable bubbles for each feature, each with its own on/off switch. When more are
  on than fit the screen, they scroll by, either continuously or pausing on each widget
  (adjustable in 0.5 s steps; 2.5 s by default). Long lines inside a widget scroll sideways so
  nothing gets cut off.
- **Themes:** Dark, Light, Miami Vice and Cyber Hacker. Landscape or portrait, adjustable
  brightness.
- **Screen off/on:** press the lower button on the back (BOOT) to turn the screen off or back on.
  Everything keeps running in the dark. While it's off, a tap also wakes it, and that tap doesn't
  press anything.
- **Status LED:** the LED on the back shows boot and alert status. You can switch it off under
  **Display & touch > Screen & LED**.

### Information

| Feature | What it shows | Source |
|---|---|---|
| **Weather** | Forecast icon and temperature for your ZIP code, refreshed every 15 min | Open-Meteo (no key) |
| **NWS alerts** | Active National Weather Service alerts, with a pop-up for new ones | api.weather.gov |
| **Buoy data** | Wave height, period and direction, wind, water temperature. Gulf, Panhandle and Atlantic buoys that report waves are built in, and you can add your own | NOAA NDBC |
| **Stocks** | Up to 8 tickers (stocks, ETFs, indexes, futures), rotating in the status bar | Yahoo Finance (unofficial) |
| **NFL scores** | Your team's game live, plus the whole week | ESPN (unofficial) |
| **Earthquakes** | Every quake in the past 24 hours, with counts for today and yesterday | USGS |
| **Volcanoes** | Elevated volcano alerts and new eruptions | USGS, Smithsonian GVP |
| **Aircraft near airport** | Airliners within 10 NM of any airport code you choose. Shows the airline and flight number, altitude, speed and climbing or descending, and where each flight is coming from and going to. Flights to or from your airport get their own widget | adsb.fi, adsbdb |
| **Breaking news** | Top headlines with summaries. Choose AP News, Fox News or CNN | Public RSS feeds |
| **Email** | Unread messages that arrived today on your own IMAP server (IMAP over TLS), with sender and subject | Your mail server |
| **Outages** | Incidents happening now at about 180 online services (Discord, Reddit, Zoom, Slack, OpenAI, GitHub, AWS, Cloudflare...), worst first, filterable by severity and provider | OutageDeck (official status pages) |

### Tools

- **Drone Remote ID:** detects drones broadcasting Remote ID over Wi-Fi beacons, Wi-Fi NaN and
  Bluetooth (DJI's built-in Remote ID included). Shows ID, position, altitude, speed, operator
  location and distance, with a signal "hunt" mode. **Watch** mode keeps a log and pops up an
  alert when a drone comes near.
- **Tracker detector:** watches for AirTag/Find My, Tile, SmartTag and Google Find My trackers and
  warns you if one stays with you for 10+ minutes.
- **Bluetooth scanner:** nearby BLE devices, with an ignore list and a hunt mode.
- **Wi-Fi analyzer:** networks per channel with a best-channel hint, viewing 2.4 GHz and 5 GHz
  separately.
- **Dual-band Wi-Fi:** joins 5 GHz when the signal is good and falls back to 2.4 GHz when it
  weakens.
- **Over-the-air updates:** new firmware can be pushed over Wi-Fi, but only after you tap
  **Accept** on the device.

---

## Getting started

1. **Flash the firmware.** Open the **[FortiGill Web Flasher](https://www.fortigill.net/flasher.html)**
   in Google Chrome, plug the board in with a USB-C data cable, and follow the three steps on the
   page. The latest release is selected for you. For other methods, see [FLASHING.md](FLASHING.md).
2. **Calibrate the touch screen.** On first boot, tap the center of each of the 4 crosses with the
   stylus. You can redo this later under **Menu > Display & touch > Calibrate touch screen**.
3. **Join Wi-Fi.** Tap the Wi-Fi icon in the status bar (or **Menu > Network settings**), pick
   your network and type the password.
4. **Set things up.** Open **Menu** and visit the features you want:
   - **Weather:** your ZIP code.
   - **Date & time:** your time zone.
   - **Aircraft near airport:** your airport code.
   - **Email:** your IMAP server, email address and password.
   - Each feature page has its own **Show on desktop** and status-bar switches.

> **Touch way off?** If you can't reach the menu, hold the stylus on the screen while pressing the
> reset button (the upper button on the back). Calibration then starts again.

---

## 3D-printed case

The repository includes an **STL for a case that fits the RockBase NM-CYD-C5**:
[`case/nm-cyd-c5-tacklebox.stl`](case/nm-cyd-c5-tacklebox.stl). It's also attached to each
release.

- **Snap-together fit:** no screws or glue. The case clicks together around the board.
- **Built-in stylus holder:** the stylus clips into the case, so it doesn't get lost.

**Printing:** _add your recommended material, layer height, infill and support settings here._

---

## Privacy and security notes

- **Saved passwords:** your Wi-Fi password and email password are stored in the board's flash
  memory, so anyone with physical access to the board could read them. For email, use an app
  password if your server supports them.
- **Email is read-only:** the board opens your inbox in read-only mode. It never marks messages
  read, moves or deletes them, and never sends anything. On the first connection it remembers
  your mail server's certificate, and if that ever changes it won't send your password until you
  confirm on screen.
- **Over-the-air updates:** these need the OTA password set in `platformio.ini` *and* a tap on
  **Accept** on the device. Release binaries use the default password `cyd_ota_change_me`. If
  you build from source, change it (see below).

---

## Building from source

The firmware is built with [PlatformIO](https://platformio.org/), as the VS Code extension or the
`pio` command line.

The ESP32-C5 needs **Arduino-ESP32 3.3.x**, which PlatformIO gets through the
[pioarduino](https://github.com/pioarduino/platform-espressif32) platform. If a build fails
because the board `esp32-c5-devkitc-1` is unknown, set the platform in the `[env:cyd-c5]`
section of `platformio.ini`:

```ini
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
```

```bash
git clone https://github.com/fortigill/tacklebox-c5.git
cd tacklebox-c5

# First time: flash over USB
pio run -e cyd-c5 -t upload
pio device monitor              # serial log at 115200

# After that: update over Wi-Fi (approve the pop-up on the device)
pio run -e cyd-c5-ota -t upload
```

**OTA password:** change `password` in the `[ota]` section of `platformio.ini` (letters, digits
and `_` only), then flash once over USB so the board learns the new one. OTA uploads go to
`tacklebox-c5.local`. If your network doesn't resolve `.local` names, set `hostname_c5` to the
board's IP address, which is shown on **Menu > About this device**.

### Also runs on the classic CYD

The same code builds for the original "Cheap Yellow Display" (ESP32-2432S028R, ESP32-WROOM-32,
ILI9341) with `pio run -e cyd -t upload`. That board has 2.4 GHz Wi-Fi only and less memory, so
some lists are shorter. The bundled case is for the NM-CYD-C5 only.

---

## Hardware

**RockBase NM-CYD-C5:** ESP32-C5 (single-core RISC-V, dual-band Wi-Fi 6, Bluetooth LE), 16 MB
flash, 8 MB PSRAM, ST7789 2.8" 240×320 display, XPT2046 resistive touch.

| Function | Pins |
|---|---|
| Display ST7789 (SPI) | SCK 6, MOSI 7, MISO 2, CS 23, DC 24, backlight 25 |
| Touch XPT2046 (same SPI bus) | CS 1 |
| SD card (unused, held deselected) | CS 10 |
| Status LED (WS2812) | 27 |
| BOOT button (screen on/off) | 28 |

The flash layout (`partitions_c5_16mb.csv`) has two 6 MB app slots for over-the-air updates,
1 MB of SPIFFS and a core-dump area.

---

## Project layout

```
platformio.ini          build environments (cyd, cyd-ota, cyd-c5, cyd-c5-ota)
partitions_c5_16mb.csv  C5 flash layout
case/                   STL for the NM-CYD-C5 case (nm-cyd-c5-tacklebox.stl)
include/                LVGL config (PSRAM allocator on the C5)
src/board.h             per-board pins and features
src/main.cpp            setup/loop, back button
src/display_c5.cpp      C5 display + touch (ESP-IDF esp_lcd), brightness, screen on/off
src/display.cpp         classic CYD display + touch (TFT_eSPI)
src/ui.cpp              all screens and desktop widgets (LVGL 8.3)
src/weather.cpp         background downloads: weather, alerts, buoys, stocks, NFL,
                        quakes, volcanoes, flights, news, email (IMAP), outages
src/net.cpp             Wi-Fi scan/connect, dual-band steering
src/drone.cpp           drone Remote ID (Wi-Fi sniffer, ODID decoder)
src/ble.cpp             BLE scanner, tracker detector, hunt modes
src/ota.cpp             over-the-air updates with on-device approval
src/led.cpp             status LED
tools/                  icon generators, build-size check, OTA helper
```

---

## Credits

Built with [LVGL](https://lvgl.io/), [ArduinoJson](https://arduinojson.org/),
[NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) and Espressif's
[Arduino-ESP32](https://github.com/espressif/arduino-esp32). Data comes from Open-Meteo, the
National Weather Service, NOAA NDBC, USGS, the Smithsonian Global Volcanism Program, adsb.fi,
adsbdb, AviationWeather.gov, Yahoo Finance, ESPN, AP News (via Flipboard), Fox News, CNN (via
Google News) and OutageDeck. The
unofficial sources can change or stop working without notice.
