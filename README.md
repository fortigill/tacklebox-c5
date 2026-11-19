# CYD Touch UI (ESP32-2432S028R)

LVGL 8.3 touch UI for the 2.8" Cheap Yellow Display. Current version: see `src/version.h` and
[CHANGELOG.md](CHANGELOG.md).

- **Desktop**: status bar with firmware version and a Wi-Fi icon (green = connected, amber =
  connecting, dim grey = offline). Tap the icon for Wi-Fi settings, or tap **Menu**.
- **Menu**: Network settings, Display & touch, About this device.
- **Wi-Fi**: async scan, networks sorted by signal, on-screen keyboard for the password, DHCP connect,
  credentials saved in NVS with auto-reconnect on boot, **Forget** to clear them. Note: the password
  is stored unencrypted in flash.
- **Display & touch**: landscape/portrait (plus flipped variants) and touch calibration.

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
pio run -t upload --upload-port /dev/cu.usbserial-3110
pio device monitor -p /dev/cu.usbserial-3110
```

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
- **Colors look wrong or inverted**: some CYDs, including the two-USB-port version, use an ST7789
  panel and need a different driver flag.

## Layout

```
platformio.ini      board, libraries, TFT_eSPI pin config
include/lv_conf.h   LVGL config
src/version.h       firmware version
src/main.cpp        setup/loop
src/display.cpp     TFT + touch drivers, rotation, calibration
src/net.cpp         Wi-Fi scan/connect/save (non-blocking state machine)
src/ui.cpp          screens
```
