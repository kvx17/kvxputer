# kvxputer

Cardputer ADV firmware — purple/green UI, Wii-style channel menu, optional Unit Scroll, and network tools. Evil-Cardputer reference material lives in local `resources/` (gitignored); see [docs/REFERENCE.md](docs/REFERENCE.md).

## What you get

- **Boot:** `kvxputer` / `v.0.1` splash (no Bruce shark animation)
- **Main menu:** 2×3 channel grid (6 apps per page), green arrows when more pages exist, selected app name in an inverted green footer bar
- **Apps:** WiFi, NetOps, BLE, RF, files, config, etc. (Bruce core + kvxputer extensions)
- **kvxkeyboard HID:** USB/Bluetooth multi-mode HID (presenter, keyboard, media, mouse, jiggler) — see [docs/HID_REMOTE.md](docs/HID_REMOTE.md)
- **kvxputer universal remote:** Infrared learn/replay pad (12-button Flipper `.ir` profiles) — see [docs/KREMOTE.md](docs/KREMOTE.md)
- **Optional:** M5 [Unit Scroll](https://docs.m5stack.com/en/unit/UNIT-Scroll) and [Unit PaHub v2.1](https://docs.m5stack.com/en/unit/Unit-PaHub%20v2.1) on Grove PORT.A

## Requirements

- [PlatformIO](https://platformio.org/) (`pio` on PATH)
- M5Stack **Cardputer** or **Cardputer ADV**
- USB data cable
- Linux: user in `uucp` / `dialout` for serial upload

## Build firmware

From the repository root:

```bash
cd /path/to/kvxputer
pio run -e m5stack-cardputer
```

**Lite build** (smaller flash, fewer features):

```bash
pio run -e m5stack-cardputer-lite
```

## Generate a flashable `.bin`

PlatformIO produces a merged image automatically after a successful build:

```text
kvxputer-m5stack-cardputer.bin
```

Path: project root (same folder as `platformio.ini`).

You can also flash the raw app image from:

```text
.pio/build/m5stack-cardputer/firmware.bin
```

(offset `0x10000` if using `esptool` manually; prefer the merged `kvxputer-*.bin` at offset `0x0`).

## Flash to device

### Option A — PlatformIO (recommended)

```bash
pio run -e m5stack-cardputer -t upload
```

Specify port if needed:

```bash
pio device list
pio run -e m5stack-cardputer -t upload --upload-port /dev/ttyACM0
```

### Option B — `esptool` + merged bin

```bash
pio pkg exec -p tool-esptoolpy -- esptool --chip esp32s3 \
  --port /dev/ttyACM0 write_flash 0x0 kvxputer-m5stack-cardputer.bin
```

If upload fails, hold the shoulder button (GPIO0) while plugging USB, then retry.

### Serial monitor

```bash
pio device monitor -e m5stack-cardputer
```

## How to use the firmware

### Main menu (Wii-style grid)

| Key | Action |
|-----|--------|
| `;` / `,` | Move up / left (previous row or item) |
| `.` / `/` | Move down / right (next row, page, or item) |
| Enter | Open selected app |
| `` ` `` / Backspace | Back (inside submenus) |
| Unit Scroll | Rotate = move selection, press = open |

The **green bar at the bottom** shows the **selected** app name (inverted colors).

### Unit Scroll

1. Plug Unit Scroll into **Grove PORT.A** (SDA=G2, SCL=G1).
2. Boot probes I2C `0x40`; if missing, keyboard-only continues.
3. **Config → System → Unit Scroll** — status, reconnect, invert direction.

### Unit PaHub v2.1

I2C multiplexer (PCA9548A) that turns PORT.A into six Grove channels. Off by default.

1. Plug PaHub into **Grove PORT.A**. Match the hub I2C address to the DIP switch (`0x70`–`0x77`, default `0x70`).
2. **Config → System → PaHub** — enable, set address (or use Reconnect to auto-detect `0x70`–`0x77`), assign each channel (RFID2, NFC, Unit Scroll, Joystick2, RF433R), Scan channels.
3. RFID → Config → RFID Module picks **M5 RFID2 (chN)** / **M5 NFC (chN)** when both are assigned.
4. Do not use Grove RF (CC1101) and PaHub on PORT.A at the same time.

### Submenus

Inside apps, navigation uses the standard list UI (same keys as above). Press back to return to the channel grid.

## Project layout

| Path | Purpose |
|------|---------|
| `src/root/` | Core app: UI, config, HAL, storage, net |
| `src/menu/<domain>/` | One folder per main-menu app |
| `tools/build/data/support_files/` | LittleFS factory seed (profiles, portals) |
| `tools/build/embedded_resources/` | WebUI assets (compiled into firmware) |
| `platformio.ini`, `tools/porting/boards/`, `lib/` | Build, board porting, and library definitions |
| `docs/ARCHITECTURE.md` | Three-root filesystem and module rules |
| `docs/REFERENCE.md` | Local-only `resources/` reference material |
| `resources/` | **Gitignored** — Evil-Cardputer `.ino` + example SD files |

Seed LittleFS after build:

```bash
pio run -e m5stack-cardputer -t uploadfs
```

## Docs

- [Evil feature map](docs/EVIL_FEATURE_MAP.md)
- [WiFi / raw frames](docs/WIFI_P0_AND_RAW_FRAMES.md)
- [kvxkeyboard HID](docs/HID_REMOTE.md)
- [HID / BadUSB](docs/HID_BADUSB.md)
- [QA matrix](docs/QA_MATRIX.md)

## Attribution

Based on [Bruce](https://github.com/BruceDevices/Firmware) (AGPL-3.0). Evil-Cardputer reference (local `resources/`, gitignored) by 7h30th3r0n3. See [NOTICE](NOTICE).
