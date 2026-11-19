# CYD Touch UI (ESP32-2432S028R)

LVGL 8.3 touch UI for the 2.8" Cheap Yellow Display:

- **Desktop**: status bar with a Wi-Fi icon (green = connected, amber = connecting, dim grey = offline) and a **Menu** button. Tapping the icon opens Wi-Fi settings.
- **Menu**: Network settings, About this device.
- **Wi-Fi settings**: async scan, networks sorted by signal (duplicates merged), tap to join, on-screen keyboard for the password, **Forget** for the saved network.
- Connects via DHCP. Credentials are saved in NVS and the device reconnects on boot. Note: the password is stored unencrypted in flash.

## 1. Confirm the board

Plug in the board, then on the Mac run:

```bash
ls /dev/cu.*                      # look for /dev/cu.usbserial-XXXX or /dev/cu.wchusbserial-XXXX
pio device list
pio pkg exec -p tool-esptoolpy -- esptool.py --port /dev/cu.usbserial-XXXX chip_id
pio pkg exec -p tool-esptoolpy -- esptool.py --port /dev/cu.usbserial-XXXX flash_id
```

The 2432S028R should report **ESP32-D0WD(-V3)** with 4 MB flash. If it reports **ESP32-C3**, the pin map below won't match. The C3 has no GPIO above 21.

## 2. Build and flash

Install PlatformIO first, either the VS Code extension or `brew install platformio`. Then:

```bash
pio run -t upload --upload-port /dev/cu.usbserial-XXXX
pio device monitor
```

If the upload fails to connect, hold **BOOT** while it says "Connecting...".

## Pin map (2432S028R)

| Function | Pins |
|---|---|
| TFT ILI9341 (HSPI) | MOSI 13, MISO 12, SCLK 14, CS 15, DC 2, BL 21 |
| Touch XPT2046 (VSPI) | CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36 |
| SD card | CS 5, MOSI 23, MISO 19, SCK 18 |
| RGB LED (active low) | R 4, G 16, B 17 |
| LDR / Speaker | 34 / 26 |

## Troubleshooting

- **Taps land in the wrong place**: set `TOUCH_DEBUG 1` in `src/display.cpp`. Tap the four corners and read the raw values in the serial monitor. Put the min/max values into `TS_MIN_X`/`TS_MAX_X`/`TS_MIN_Y`/`TS_MAX_Y`. If an axis is mirrored, swap its min and max.
- **Colors look wrong or inverted**: some CYDs, including the two-USB-port version, use an ST7789 panel instead of the ILI9341. The display driver flag in `platformio.ini` has to change for those.
- **Blank screen with the backlight on**: check that `TFT_BL=21` is set and that the panel driver matches your board.

## Layout

```
platformio.ini      board, libraries, TFT_eSPI pin config
include/lv_conf.h   LVGL config
src/main.cpp        setup/loop
src/display.cpp     TFT + touch + LVGL drivers
src/net.cpp         Wi-Fi scan/connect/save (state machine, non-blocking)
src/ui.cpp          screens
```
