# Flashing SaltyGills Tackle Box onto the RockBase NM-CYD-C5

This guide puts the firmware on your board for the first time, or updates it, on **Windows,
macOS or Linux**. Choose one method:

| Method | Best for | Needs |
|---|---|---|
| [A. Browser flasher](#method-a-browser-flasher-easiest) | Most people | Chrome or Edge. Nothing to install |
| [B. esptool command line](#method-b-esptool-command-line) | Comfortable in a terminal, or a browser that can't see the board | Python 3 |
| [C. Build from source](#method-c-build-from-source-platformio) | Changing code or your OTA password | PlatformIO |

After the first flash, later updates can also go [over Wi-Fi](#updating-later-over-wi-fi).

---

## What you need

- The **RockBase NM-CYD-C5** board.
- A **USB-C data cable.** Some cables only carry power. If your computer never sees the board,
  try another cable first.
- The firmware file from the
  [Releases page](https://github.com/fortigill/tacklebox-c5/releases): download
  **`tacklebox-c5-v1.13.0.bin`**. It's a single combined image that is written at address
  **`0x0`**.

> **Your settings:** flashing over an existing install keeps your Wi-Fi, ZIP code and other
> settings. Only **Erase flash** (or `erase-flash`) wipes them.

### The two buttons on the back

| Button | Position | What it does |
|---|---|---|
| **RST** (reset) | Upper | Restarts the board |
| **BOOT** | Lower | Screen off/on while running. Held during reset, it puts the board into download mode |

**Download mode, if a flasher can't connect:** hold **BOOT**, tap **RST**, then release **BOOT**.
The screen stays blank, and the board waits for new firmware. Press **RST** after flashing to
start normally.

---

## Method A: Browser flasher (easiest)

This works the same on Windows, macOS and Linux. It needs **Google Chrome** or **Microsoft Edge**
on a computer; Safari and Firefox can't talk to USB serial devices.

1. Plug the board into your computer.
2. Open **<https://espressif.github.io/esptool-js/>**.
3. Set **Baudrate** to `460800`, then click **Connect**.
4. Pick the board's port in the pop-up, then click **Connect**. It's usually named
   "USB JTAG/serial debug unit", "USB Serial" or "CH340". The page should then report an
   **ESP32-C5**. If nothing shows up, see [Troubleshooting](#troubleshooting).
5. Under **Flash Address**, enter `0x0`, and choose `tacklebox-c5-v1.13.0.bin` as the file.
6. Click **Program** and wait for it to finish (about a minute).
7. Click **Disconnect**, then press **RST** on the board.

For a completely fresh start that wipes all settings, click **Erase Flash** before **Program**.

---

## Method B: esptool command line

### 1. Install esptool (version 4.8 or newer)

**Windows** (PowerShell or Command Prompt). Install Python 3 from
[python.org](https://www.python.org/downloads/) first, ticking "Add python.exe to PATH":

```powershell
py -m pip install --upgrade esptool
```

**macOS** (Terminal):

```bash
python3 -m pip install --user --upgrade esptool
# or, with Homebrew:
brew install esptool
```

**Linux** (terminal):

```bash
python3 -m pip install --user --upgrade esptool
# or: pipx install esptool
```

Check that it works with `esptool version`. Older installs name the command `esptool.py`.

### 2. Find the board's port

Plug the board in, then:

| System | How to look | Typical name |
|---|---|---|
| **Windows** | Device Manager > **Ports (COM & LPT)** | `COM3`, `COM5`... |
| **macOS** | `ls /dev/cu.*` | `/dev/cu.usbmodem1101` or `/dev/cu.usbserial-1110` |
| **Linux** | `ls /dev/ttyACM* /dev/ttyUSB*` | `/dev/ttyACM0` or `/dev/ttyUSB0` |

If you're not sure which one it is, run the command once with the board unplugged and once with it
plugged in. The new entry is the board.

**Linux only:** let your user open serial ports, then log out and back in:

```bash
sudo usermod -aG dialout $USER     # Arch/Manjaro: use the "uucp" group instead
```

### 3. Flash

Run the command from the folder where you saved the `.bin` file, using your own port in place of
the example.

**Windows:**

```powershell
py -m esptool --chip esp32c5 --port COM5 --baud 460800 write-flash 0x0 tacklebox-c5-v1.13.0.bin
```

**macOS:**

```bash
esptool --chip esp32c5 --port /dev/cu.usbmodem1101 --baud 460800 write-flash 0x0 tacklebox-c5-v1.13.0.bin
```

**Linux:**

```bash
esptool --chip esp32c5 --port /dev/ttyACM0 --baud 460800 write-flash 0x0 tacklebox-c5-v1.13.0.bin
```

- **Using esptool 4.x?** The command is `esptool.py` and the subcommand is `write_flash`, with an
  underscore.
- **Clean slate:** to wipe all settings first, run the same command with `erase-flash` in place of
  `write-flash 0x0 tacklebox-c5-v1.13.0.bin`, then flash.

When it finishes, press **RST** if the board doesn't restart by itself.

---

## Method C: Build from source (PlatformIO)

Use this if you want to change code, or set your own OTA password, which is recommended.

1. **Install PlatformIO.** Either install
   [VS Code](https://code.visualstudio.com/) and its **PlatformIO IDE** extension, or install the
   command line: `pipx install platformio` (it needs Python 3.10–3.14).
2. **Get the code:**

   ```bash
   git clone https://github.com/fortigill/tacklebox-c5.git
   cd tacklebox-c5
   ```

3. **Set your OTA password (optional but recommended).** Edit `password` under `[ota]` in
   `platformio.ini`, using letters, digits and `_` only.
4. **Build and flash over USB:**

   ```bash
   pio run -e cyd-c5 -t upload
   ```

   In VS Code, choose the **env:cyd-c5** environment, then click **Upload**. PlatformIO usually
   finds the port itself. If it doesn't, add `--upload-port COM5` (or your port).
5. **Watch the log (optional):** `pio device monitor`, at 115200 baud.

The first build downloads the ESP32-C5 toolchain and can take several minutes. If PlatformIO says
the board `esp32-c5-devkitc-1` is unknown, see
[Building from source](README.md#building-from-source) in the README.

---

## Updating later over Wi-Fi

Once the board runs Tackle Box and is on your Wi-Fi, you don't need the USB cable for updates:

```bash
pio run -e cyd-c5-ota -t upload
```

- **Approve it on the device:** a **Firmware update** pop-up appears there. Tap **Accept** within
  30 seconds, or the update is refused and the current firmware keeps running.
- **Password:** the upload uses the OTA password from `platformio.ini`, which must match the one
  the board was built with. Release binaries use `cyd_ota_change_me`.
- **Address:** the board is found as `tacklebox-c5.local`. If that doesn't resolve, put the
  board's IP address (shown on **Menu > About this device**) in `hostname_c5` in
  `platformio.ini`.

You can also update with Method A or B. Flashing a newer release `.bin` over USB keeps your
settings.

---

## After flashing: first boot

1. **The LED** blinks red while starting, then turns green once Wi-Fi has settled. It stays off if
   you switched it off in the settings.
2. **Touch calibration** runs on the first boot: tap the center of each of the 4 crosses.
3. **Wi-Fi:** tap the Wi-Fi icon in the status bar, pick your network and enter the password.
4. Head to **Menu** to set up weather, time zone, airport, email and the rest. The
   [README](README.md#getting-started) walks through it.

---

## Troubleshooting

**The board doesn't show up as a port, or the browser lists nothing**
- Try a different USB-C cable (it must carry data) and a different USB port. Avoid hubs.
- Put the board in download mode: hold **BOOT**, tap **RST**, release **BOOT**. Then try again.
- **Windows:** if Device Manager shows an unknown device or "USB Serial" with a warning sign,
  install the [CH340 driver](https://www.wch-ic.com/downloads/CH341SER_EXE.html) and replug.
- **macOS:** if a "USB accessory" prompt appears, click **Allow**. On macOS 10.x, install the CH340
  driver.
- **Linux:** check that you're in the `dialout` group (above). Run `dmesg | tail` right after
  plugging in to see the device name.

**"Failed to connect", "No serial data received", or the port is busy**
- Close anything else using the port: PlatformIO's serial monitor, Arduino IDE, a terminal, or
  another browser tab.
- Use download mode (above), then retry.
- Lower the speed: `--baud 115200`, or 115200 in the browser flasher.

**"This chip is ESP32-xx, not ESP32-C5" or "Wrong chip"**
- You've picked the wrong port, or this isn't an NM-CYD-C5. The classic CYD needs a different
  build (`pio run -e cyd -t upload`).

**Flashing succeeded but the screen stays blank**
- Press **RST** once without holding **BOOT**. A board started in download mode stays blank.
- If it still won't start, erase and flash again (Method A: **Erase Flash**, then **Program**;
  Method B: `erase-flash`, then `write-flash`). This wipes your settings.
- Watch the serial log (`pio device monitor`, or **Serial Monitor** at 115200 baud in the
  browser flasher). The first lines show the version and the reason for the last restart.

**Touch is way off after flashing**
- Hold the stylus on the screen while pressing **RST**, and calibration starts again.

**Colors look wrong**
- **Menu > Display & touch > Colors:** toggle **Invert colors** or **Swap red/blue**.

---

## For maintainers: making the release `.bin`

Build with PlatformIO, then combine the four pieces into one image that's written at `0x0`. The
ESP32-C5 bootloader sits at `0x2000`.

```bash
pio run -e cyd-c5

B=.pio/build/cyd-c5
esptool --chip esp32c5 merge-bin -o tacklebox-c5-v1.13.0.bin \
  --flash-mode keep --flash-freq keep --flash-size 16MB \
  0x2000  $B/bootloader.bin \
  0x8000  $B/partitions.bin \
  0xe000  ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 $B/firmware.bin
```

Attach `tacklebox-c5-v1.13.0.bin` to the GitHub release. If `boot_app0.bin` isn't at that path,
find it with `find ~/.platformio -name boot_app0.bin`.
