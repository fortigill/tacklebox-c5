# Changelog

All notable changes to this project. Versions follow [Semantic Versioning](https://semver.org/);
the version string lives in `src/version.h`.

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
