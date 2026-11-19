# Changelog

All notable changes to this project. Versions follow [Semantic Versioning](https://semver.org/);
the version string lives in `src/version.h`.

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
