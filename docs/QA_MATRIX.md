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

## Lite env

`tools/porting/boards/m5stack-cardputer/m5stack-cardputer.ini` sibling env `m5stack-cardputer-lite` disables `EVIL_EXTENSIONS` and enables `LITE_VERSION` when flash is tight.
