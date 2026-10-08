# QA matrix (kvxputer Cardputer ADV)

Source domains: `src/menu/<id>/` — see [ARCHITECTURE.md](ARCHITECTURE.md).

| Scenario | Expect |
|----------|--------|
| Boot without Unit Scroll | Silent; keyboard carousel works |
| Boot with Unit Scroll on PORT.A | Dim green LED; CW/CCW navigates; press selects |
| Config → Unit Scroll → Reconnect | Status updates; success/fail message |
| Unit Scroll unplug mid-session | Next poll marks absent; keyboard still works |
| Grove RF (CC1101) + Scroll | Status shows Grove busy; prefer one accessory |
| PaHub missing, PaHub off | Boot + RFID2 on PORT.A + Scroll unchanged |
| PaHub on, hub missing | Config → PaHub shows Not found; I2C RFID fails clearly; SPI RFID still works |
| PaHub empty hub | Scan shows no slaves; Read tag fails cleanly |
| RFID2 on PaHub ch0 | RFID → Read tag succeeds |
| NFC (PN532) on PaHub ch1 | Read tag + NDEF emulate succeed |
| RFID2 + NFC assigned | RFID Config switches reader; title shows channel |
| Scroll on ch2, RFID on ch0 | Menus scroll when RFID app is closed; during Read tag Scroll skips, keyboard works |
| RFID → Config → Wiring help | Lists Overview / RFID2 / PN532 I2C+SPI / RC522; scrollable pinouts match PORT.A |
| PN532 I2C on PORT.A (Elechouse/ITEAD) | RFID Module → PN532 on I2C; Read/Write succeed |
| PN532 read → save → load → emulate | Dump in `/support_files/rfid/`; Emulate tag interacts with phone/reader |
| PN532 Emulate NDEF | RFID → Emulate NDEF (Text/URL) without prior dump |
| RFID2 selected | Emulate tag / Emulate NDEF hidden; Read/Write/Clone still work |
| PN532 on SPI (defaults) | Module → PN532 on SPI; Pins Setup → PN532 Pins shows 40/39/14/1; SD still on CS 12 |
| Grove RF (CC1101) vs PaHub | Status shows Grove busy |
| Pins → I2C Finder with PaHub on | Lists hub address and per-channel slaves |
| Lite env | `m5stack-cardputer-lite` still links PaHub HAL + menu |
| `menu/wifi` → Evil Portal / Wifi Atks | P0 WiFi path works |
| `menu/wifi` → Probes / Handshakes / Wall Of Flipper / Dead Drop / Open Wifi / Aircrack / CSI Radar / C5 Serial | Addon menus open (`EVIL_EXTENSIONS`) |
| `menu/netops` carousel tile | Present after WiFi; Bruce rows + P1/P2 addon menus |
| `menu/ble` → kvxkeyboard HID + Name Flood / AirTag / FindMy / Skimmer | HID works; addons open |
| `menu/wifi` → C5 Serial | UART host; optional companion `.bin` list |
| `menu/infrared` → TagTinker ESL | PP4 ping/LED/page-flip; assets on SD `IR_ESL` |
| `menu/wifi` → CSI Radar | RSSI hop / STA CSI / ESP-NOW |
| `menu/netops` → SkyJack / LDAP / Autodiscover / CIW / Hijack | Addon logic runs (`EVIL_EXTENSIONS`) |
| `menu/others` → LLM Chat | HTTP stream; UART if `HAS_LLM_MODULE` |
| Modules → Companion bins | Lists `/support_files/companions/*.bin` or explains SD pack |
| SD missing | Optional assets error; firmware still runs |
| SD present with `tools/sd_pack` | Wordlists, CIW JSON, companion bins resolve |
| `menu/gps` → Wardriving → Wardriving Master | Addon under existing Wardriving submenu |
| `menu/infrared` → kvxputer universal remote | Menu opens; Learn/Use/Delete/Button Map/Settings/About |
| Universal remote learn all 12 | Saves `kremote_<name>.ir` under `/support_files/infrared/remotes/` |
| Universal remote skip mid-learn | Esc early-save keeps accepted slots; Right skips a button |
| Universal remote Use short/hold | Directions vs Vol/Ch; OK vs Home; flash highlights sector |
| Universal remote Back ×2 | Double-tap Back sends POWER |
| Universal remote Swapped | Short/hold inverted for dual-action keys |
| Universal remote Portrait | Pad rotates; exits restore previous rotation |
| Universal remote Custom IR | `kremote_*.ir` opens in Custom IR / Flipper-compatible |
| Lite env + universal remote | `m5stack-cardputer-lite` still links kvxputer universal remote |
| Flash size | Full build ~4.2 MB app image on 8 MB (fits `custom_8Mb`); lite if oversize |
| Legacy SD paths | First boot migrates `/Bruce*` → `/support_files/*` |

## M5StickS3 (`m5stack-sticks3`)

Same branch as Cardputer; feature limits are board HAL + flags (no keyboard, 2 buttons). Merged artifact: `kvxputer-m5stack-sticks3.bin`.

| Scenario | Expect |
|----------|--------|
| Boot | Channel menu scrolls 1→2→3…; side tap=next / hold=prev; main tap=OK / hold=back |
| Infrared enter/exit | EXT 5V on while in IR menu; speaker amp muted for RX; restored EXT on exit |
| USB HID | `USB_as_HID`; kvxkeyboard modes usable without physical keyboard |
| BLE HID | Pair/connect via buttons; host-slot number keys N/A |
| WiFi / NetOps / BLE Evil | Menus present when `EVIL_EXTENSIONS=1` (default on StickS3) |
| Hat SPI (SD / CC1101 / NRF24) | Per `tools/porting/boards/m5stack-sticks3/connections.md` |
| Cardputer regression | `pio run -e m5stack-cardputer` still succeeds after StickS3 shared-src changes |

## M5Stack Tab5 (`m5stack-tab5`)

Same branch as Cardputer; feature limits are board HAL + flags (keyboard + touch; WiFi/BLE gated). Merged artifact: `kvxputer-m5stack-tab5.bin`. Pin matrix: `tools/porting/boards/m5stack-tab5/connections.md`.

| Scenario | Expect |
|----------|--------|
| Boot | 3×6 channel grid on 1280×720; brightness and battery via M5Unified |
| Tab5 Keyboard | Esc / arrows / Enter / letters drive menus; `keyboard()` prompts accept text; `'1'`–`'6'` land in `KeyStroke.word` |
| Keyboard absent | Touch-only navigation still works |
| Touch tap tile | Opens selected channel (Sel) |
| Touch swipe left/right | Main grid and submenus: left → Next, right → Prev |
| Touch hold ≥ 3000 ms | EscPress (Back); Core2 `touchHeatMap` unchanged |
| WiFi / BLE / Discover / NetOps tiles | Visible on main grid; attack/NimBLE features still gated inside (`radio later`) |
| WiFi Connect (STA) | Scan lists APs; connect with password; no solid-cyan crash on enter/exit |
| Cyan / freeze after WiFi | Should not occur — Tab5 keeps UI canvas suppressed (direct DSI draws only) |
| Infrared enter/exit | EXT 5V on while in IR menu; restored on exit |
| Grove Scroll / RFID2 | Probe-at-runtime on PORT.A (G53/G54) |
| SD | Mounts on SPI (CS 42 / SCK 43 / MISO 39 / MOSI 44) |
| WiFi attack / CSI / Evil Portal / NimBLE HID | **Not in milestone 1** (`radio later`) |
| Cardputer regression | `pio run -e m5stack-cardputer` still succeeds after Tab5 shared-src changes |

### App enter/exit (every `kMenus` channel)

Rule: missing SPI pin or forbidden radio → message and return to submenu; Grove module on PORT.A → existing feature; no cyan screen; Cardputer and StickS3 menus unchanged.

| Channel | Expect on Tab5 |
|---------|----------------|
| WiFi | Connect / AP / TCP / SSH / Scan Hosts / WireGuard / Roku / Pass Recovery / PineAP / **kvx wifi analyzer** (STA scan on hosted C6). Wifi Atks, Evil Portal, Sniffer, Jam Detect, Karma, Kvxgotchi → `radio later` |
| Discovery | STA tools (analyzer, pineap, roku, …) listed; attack/NimBLE rows stay off. RF/NRF spectrum refuse CS/CE=-1 |
| NetOps | SSH / Scan Hosts / TCP / Responder / Reverse Shell (STA). Evil entries compiled out |
| BLE | USB `kvxkeyboard HID` works; NimBLE BLE scan/apps still gated (P4). Message names NimBLE/P4 |
| Main grid touch | Tap opens the **touched** tile (not the keyboard highlight) |
| USB | `USB_as_HID=1`: kvxkeyboard HID / BadUSB / Clicker / U2F over Type-C OTG |
| RF | Grove single-pin TX/RX on G53/G54. CC1101 with CS/GDO0=-1 → `CC1101: set CS/GDO0 in Pins (M5-Bus)` before any GPIO |
| NRF24 | CS/CE=-1 → `NRF24: set CS/CE in Pins (M5-Bus)`; no UART fallback on Tab5 |
| LoRa | `LoRa pins not configured` when CS/IRQ=-1 (existing) |
| FM | Si4713 absent → `Si4713 not found Grove PORT.A G53/G54` |
| Infrared | EXT 5V while open; TX/RX Grove; exit restores EXT |
| Ethernet | W5500 CS=-1 → `W5500 Pins not set` (existing) |
| USB | HID gadget / kvxkeyboard → gated message. BadUSB CH9329 uses Grove UART. PC Connect / Mass Storage stay; fail with message if SD/USB init fails |
| GPS | UART on PORT.A; absent → `GPS not Found! Grove PORT.A G53/G54` |
| RFID | TagOMatic probe. EMV needs PN532; begin fail names Grove PORT.A. BLE RFID stubs stay gated |
| Files / Scripts | SD/LittleFS; BJS sprites capped at 64KB on Tab5 |
| Clock / Charge | System time (no `HAS_RTC` / no `_rtc`); Esc exits |
| Tools | Calculator / QR / keyboard tools. Drone ID WiFi/BLE → `radio later`. iButton refuses G0/keyboard/sys/SDIO/SD pins |
| Modules | PaHub probe on PORT.A |
| Config | Pins can set CC1101/NRF24 CS; Advanced BLE API gated |

| StickS3 / Cardputer regression | `pio run -e m5stack-sticks3` and `pio run -e m5stack-cardputer` succeed; attack menus still run on those boards |

## Lite env

`tools/porting/boards/m5stack-cardputer/m5stack-cardputer.ini` sibling env `m5stack-cardputer-lite` disables `EVIL_EXTENSIONS` and enables `LITE_VERSION` when flash is tight.
