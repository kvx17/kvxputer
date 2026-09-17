# kvxputer

Firmware for the M5Stack **Cardputer** and **Cardputer ADV** — a pocket toolkit for wireless research, network analysis, and hardware experimentation. A secondary PlatformIO env also builds for **M5StickS3** (same branch, hardware-limited).

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
- **StickS3** — same firmware tree via `m5stack-sticks3` (2 buttons, no keyboard; see below)

---

## Requirements

- [PlatformIO](https://platformio.org/) (`pio` on PATH)
- M5Stack Cardputer or Cardputer ADV (primary), or M5StickS3 (secondary)
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

M5StickS3 (button nav, no keyboard; Evil extensions on when flash allows):

```bash
pio run -e m5stack-sticks3
```

A successful build writes a merged flash image at the project root:

```text
kvxputer-m5stack-cardputer.bin
kvxputer-m5stack-sticks3.bin
```

The raw app image also lives at `.pio/build/<env>/firmware.bin` (offset `0x10000` if flashing manually). Prefer the merged `kvxputer-*.bin` at offset `0x0`.

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

StickS3:

```bash
pio run -e m5stack-sticks3 -t upload
```

**esptool + merged bin**

```bash
pio pkg exec -p tool-esptoolpy -- esptool --chip esp32s3 \
  --port /dev/ttyACM0 write_flash 0x0 kvxputer-m5stack-cardputer.bin
```

```bash
pio pkg exec -p tool-esptoolpy -- esptool --chip esp32s3 \
  --port /dev/ttyACM0 write_flash 0x0 kvxputer-m5stack-sticks3.bin
```

If upload fails, hold the shoulder button (GPIO0) while plugging USB, then retry.

**Serial monitor**

```bash
pio device monitor -e m5stack-cardputer
```

---

## SD card

Copy the **contents** of [`tools/sd_pack/`](tools/sd_pack/README.md) to the microSD **root** (so the card has `/support_files/`, `/root/themes/`, `/menu/scripts/`). Firmware checks SD first, then LittleFS.

```text
/support_files/
  companions/          companion *.bin (build companion-* envs)
  wifi/                portals, wordlists, deaddrop, probes, …
  netops/              ciw, cctv, crawler, printer, responder
  infrared/profiles/   Flipper-style .ir library
  infrared/esl/        ESL bitmaps
  ble/  rfid/  media/
/root/themes/
/menu/scripts/         BadUSB / interpreter samples
```

| Source | Goes where |
|--------|------------|
| `tools/sd_pack/` contents | SD card root (paths above) |
| Companion `.bin` (build `companion-*`) | SD → `/support_files/companions/` — on device: **Modules → Companion bins** |
| `tools/build/data/support_files/` | LittleFS via `uploadfs` (small factory seed) |
| `kvxputer-m5stack-cardputer.bin` | Flash over USB (not the SD card) |

The pack is seeded from the LittleFS factory data plus copies of `resources/sd_files/` and Evil `SD-Card-File/` (resources are left in place). Full detail: [SD pack](tools/sd_pack/README.md), [Reference / storage](docs/REFERENCE.md).

---

## M5StickS3 notes

Same branch and shared `src/` as Cardputer; board HAL and flags live under `tools/porting/boards/m5stack-sticks3/`.

| | Cardputer | StickS3 |
|---|-----------|---------|
| Input | Full keyboard (+ optional Unit Scroll) | Side: tap = next item, hold = previous (scroll order). Main: tap = OK, hold = back |
| USB / BLE HID | Yes | Yes (button / on-screen entry; no number-key host slots) |
| IR | TX + Grove | TX/RX onboard; EXT 5V enabled in Infrared menu |
| Flash artifact | `kvxputer-m5stack-cardputer.bin` | `kvxputer-m5stack-sticks3.bin` |

Cardputer ADV extras (TCA8418 keyboard path, LoRa Cap, Unit Scroll/PaHub defaults) stay off on StickS3.
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

### NFC / RFID

Wired HF readers are selected under **RFID → Config → RFID Module**. On-device pinouts and switch settings are under **RFID → Config → Wiring help**. Elechouse PN532 V3 and ITEAD/Katranji PN532 boards use the existing **PN532 on I2C** or **PN532 on SPI** options (same driver; no separate module entry). Default remains **M5 RFID2**.

| Module | Chip | RFID Module choice | Emulate |
|--------|------|--------------------|---------|
| M5 Unit RFID2 | WS1850S | M5 RFID2 | No |
| Elechouse PN532 V3 / ITEAD PN532 | PN532 | PN532 on I2C or SPI | Yes (NDEF / T4T limits) |
| RC522 SPI | MFRC522 | RC522 on SPI | No |
| ST25R3916 | ST25R3916 | ST25R SPI/I2C (non-lite) | Yes |

Companion apps (**Chameleon**, **PN532 BLE**, **PN532 UART**) talk over BLE/UART and are separate from RFID Module.

#### Cardputer Adv — PN532 I2C (recommended)

Use Grove **PORT.A**. Do **not** wire to Adv sys I2C (G8/G9, keyboard).

| PN532 | Cardputer Adv | Grove wire (typical) |
|-------|---------------|----------------------|
| VCC | 5V | red |
| GND | GND | black |
| SDA | **G2** | yellow |
| SCL | **G1** | white |

| Board | Mode | Switches | RFID Module |
|-------|------|----------|-------------|
| [Elechouse V3](https://www.elechouse.com/elechouse/images/product/PN532_module_V3/PN532_%20Manual_V3.pdf) | I2C | CH1 **ON**, CH2 **OFF** | PN532 on I2C |
| [ITEAD / Katranji](https://cms.katranji.com/web/content/924513) | I2C | SET0 **H**, SET1 **L** | PN532 on I2C |

IRQ/RST are unused for this I2C path unless a board build defines `PN532_IRQ` / `PN532_RF_REST`.

#### Cardputer Adv — PN532 SPI (advanced)

Shared SPI with SD (SD CS stays on G12). I2C and SPI modes are mutually exclusive on the module switches; SPI CS uses G1 (`GROVE_SCL`), so do not run I2C RFID on PORT.A at the same time.

| PN532 | GPIO |
|-------|------|
| SCK | 40 |
| MISO | 39 |
| MOSI | 14 |
| SS/CS | 1 |
| VCC / GND | 5V / GND |

| Board | SPI switches |
|-------|----------------|
| Elechouse V3 | CH1 **OFF**, CH2 **ON** |
| ITEAD | SET0 **L**, SET1 **H** |

Then **RFID Module → PN532 on SPI**. Override pins under **Config → Pins Setup → PN532 Pins** if needed.

#### Methods

| Method | Where | Needs |
|--------|-------|-------|
| Read / Scan / Write / Clone / Erase | Tag-O-Matic | Wired module |
| Save / Load `.rfid` | after Read, or Load file | `/support_files/rfid/` |
| Emulate tag | after Read or Load | PN532 or ST25 |
| Emulate NDEF | RFID → Emulate NDEF | PN532 or ST25 |
| Write NDEF | RFID → Write NDEF | Wired module |
| Read EMV / SRIX | dedicated menu rows | PN532 (SRIX = I2C only) |

#### Workflow: read → save → emulate

1. Wire PN532 for **I2C** (switches as above).
2. **RFID → Config → RFID Module → PN532 on I2C**.
3. **RFID → Read tag**, hold the card until the dump finishes, press OK.
4. **Save file** (default name = UID) → `support_files/rfid/<name>.rfid`.
5. **Emulate tag** in the same session, or later **Load file** → **Emulate tag**.
6. Hold a phone/reader near the antenna; `` ` `` / Backspace stops.

**Limits:** RFID2/RC522 do not emulate (menu hides Emulate). PN532 Classic dumps are served as NDEF over Type 4 Tag, not full Classic card emulate. FeliCa emulate is IDm/PMm identity only.

**Conflicts:** Do not share PORT.A between Grove RF (CC1101) and PaHub/I2C RFID at once. With PaHub, assign an NFC channel and use **M5 NFC (chN)** / PN532 I2C as documented above.

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
| `tools/sd_pack/` | Copy-to-SD assets and companion `.bin` |
| `tools/companions/` | Optional extra-ESP sketches (not Cardputer firmware) |
| `resources/` | Gitignored reference material only — not compiled |

---

## Docs

- [Architecture](docs/ARCHITECTURE.md)
- [Feature map](docs/EVIL_FEATURE_MAP.md)
- [Reference / storage](docs/REFERENCE.md)
- [SD pack](tools/sd_pack/README.md)
- [Companion firmware](tools/companions/README.md)
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
