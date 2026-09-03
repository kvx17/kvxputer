# kvxputer

Firmware for the M5Stack **Cardputer** and **Cardputer ADV** — a pocket toolkit for wireless research, network analysis, and hardware experimentation.

kvxputer started with ideas from [Bruce](https://github.com/BruceDevices/Firmware) and [Evil-Cardputer](https://github.com/7h30th3r0n3/Evil-M5project). The architecture, UI, module layout, and feature set have diverged far enough that it stands on its own. What remains is a new firmware with its own identity, not a maintained fork.

Built for people who carry a Cardputer into labs, classrooms, and authorized assessments — and expect the device to keep up.

---

## Highlights

- **Channel menu** — Wii-style 2×3 grid, purple/green theme, inverted footer for the selected app
- **Wireless toolkit** — WiFi, BLE, RF, NRF24, Infrared, RFID, GPS, and more
- **Network security tools** — scanning, rogue services, handshake capture, DHCP/DNS tricks, NTLM, portals, and related NetOps modules (authorized use only)
- **HID & BadUSB** — multi-mode USB/Bluetooth HID (presenter, keyboard, media, mouse, jiggler) via [kvxkeyboard](docs/HID_REMOTE.md)
- **Universal remote** — learn/replay IR profiles (Flipper `.ir`) with [kremote](docs/KREMOTE.md)
- **Optional Grove gear** — [Unit Scroll](https://docs.m5stack.com/en/unit/UNIT-Scroll) and [Unit PaHub v2.1](https://docs.m5stack.com/en/unit/Unit-PaHub%20v2.1) on PORT.A
- **Lite build** — smaller flash footprint when you do not need every module

---

## Requirements

- [PlatformIO](https://platformio.org/) (`pio` on PATH)
- M5Stack Cardputer or Cardputer ADV
- USB data cable
- Linux: membership in `uucp` / `dialout` for serial upload

---

## Build

```bash
cd /path/to/kvxputer
pio run -e m5stack-cardputer
```

Lite (fewer features, smaller image):

```bash
pio run -e m5stack-cardputer-lite
```

A successful build writes a merged flash image at the project root:

```text
kvxputer-m5stack-cardputer.bin
```

The raw app image also lives at `.pio/build/m5stack-cardputer/firmware.bin` (offset `0x10000` if flashing manually). Prefer the merged `kvxputer-*.bin` at offset `0x0`.

Seed LittleFS after build when you want factory profiles and portals on device:

```bash
pio run -e m5stack-cardputer -t uploadfs
```

---

## Flash

**PlatformIO (recommended)**

```bash
pio run -e m5stack-cardputer -t upload
```

```bash
pio device list
pio run -e m5stack-cardputer -t upload --upload-port /dev/ttyACM0
```

**esptool + merged bin**

```bash
pio pkg exec -p tool-esptoolpy -- esptool --chip esp32s3 \
  --port /dev/ttyACM0 write_flash 0x0 kvxputer-m5stack-cardputer.bin
```

If upload fails, hold the shoulder button (GPIO0) while plugging USB, then retry.

**Serial monitor**

```bash
pio device monitor -e m5stack-cardputer
```

---

## Using the device

### Main menu

| Key | Action |
|-----|--------|
| `;` / `,` | Move up / left |
| `.` / `/` | Move down / right |
| Enter | Open selected app |
| `` ` `` / Backspace | Back (in submenus) |
| Unit Scroll | Rotate = select, press = open |

The green bar at the bottom shows the selected app name.

### Unit Scroll

1. Plug into Grove **PORT.A** (SDA=G2, SCL=G1).
2. Boot probes I2C `0x40`; if absent, keyboard-only continues.
3. **Config → System → Unit Scroll** — status, reconnect, invert direction.

### Unit PaHub v2.1

I2C mux (PCA9548A) that splits PORT.A into six Grove channels. Off by default.

1. Plug PaHub into PORT.A. Match hub address to the DIP switch (`0x70`–`0x77`, default `0x70`).
2. **Config → System → PaHub** — enable, set or auto-detect address, assign channels (RFID2, NFC, Unit Scroll, Joystick2, RF433R), scan.
3. RFID → Config → RFID Module can pick **M5 RFID2 (chN)** / **M5 NFC (chN)** when both are assigned.
4. Do not share PORT.A between Grove RF (CC1101) and PaHub at the same time.

Inside apps, navigation uses the same keys; back returns to the channel grid.

---

## Project layout

| Path | Purpose |
|------|---------|
| `src/root/` | Core: UI, config, HAL, storage, net |
| `src/menu/<domain>/` | One folder per main-menu app |
| `tools/build/data/support_files/` | LittleFS factory seed |
| `tools/build/embedded_resources/` | WebUI assets baked into firmware |
| `platformio.ini`, `tools/porting/boards/`, `lib/` | Build and board porting |
| `docs/` | Architecture, feature maps, HID/IR guides |
| `resources/` | Gitignored reference material only — not compiled |

---

## Docs

- [Architecture](docs/ARCHITECTURE.md)
- [Feature map](docs/EVIL_FEATURE_MAP.md)
- [WiFi / raw frames](docs/WIFI_P0_AND_RAW_FRAMES.md)
- [kvxkeyboard HID](docs/HID_REMOTE.md)
- [HID / BadUSB](docs/HID_BADUSB.md)
- [kremote IR](docs/KREMOTE.md)
- [QA matrix](docs/QA_MATRIX.md)

---

## License & credits

**AGPL-3.0-or-later.** See [LICENSE](LICENSE) and [NOTICE](NOTICE).

Inspired by Bruce (BruceDevices) and Evil-Cardputer (7h30th3r0n3). kvxputer reworks those foundations heavily — new shell, menu model, paths, and a large set of security-oriented modules — and is maintained as an independent project. Upstream copyright notices remain where applicable; combined distribution is AGPL-3.0 because of the Bruce lineage.

Use responsibly: only on networks and devices you own or have explicit permission to test.
