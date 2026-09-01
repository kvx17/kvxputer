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
| Grove RF (CC1101) vs PaHub | Status shows Grove busy |
| Pins → I2C Finder with PaHub on | Lists hub address and per-channel slaves |
| Lite env | `m5stack-cardputer-lite` still links PaHub HAL + menu |
| `menu/wifi` → Evil Portal / Wifi Atks | P0 WiFi path works |
| `menu/wifi` → Probes / Handshakes / Wall Of Flipper / Dead Drop / Open Wifi / Aircrack / CSI Radar / C5 Serial | Addon menus open (`EVIL_EXTENSIONS`) |
| `menu/netops` carousel tile | Present after WiFi; Bruce rows + P1/P2 addon menus |
| `menu/ble` → kvxkeyboard HID + Name Flood / AirTag / FindMy | HID works; addons open |
| `menu/gps` → Wardriving → Wardriving Master | Addon under existing Wardriving submenu |
| `menu/infrared` → TagTinker ESL | Addon listed with other IR tools |
| `menu/infrared` → kvxputer universal remote | Menu opens; Learn/Use/Delete/Button Map/Settings/About |
| Universal remote learn all 12 | Saves `kremote_<name>.ir` under `/support_files/infrared/remotes/` |
| Universal remote skip mid-learn | Esc early-save keeps accepted slots; Right skips a button |
| Universal remote Use short/hold | Directions vs Vol/Ch; OK vs Home; flash highlights sector |
| Universal remote Back ×2 | Double-tap Back sends POWER |
| Universal remote Swapped | Short/hold inverted for dual-action keys |
| Universal remote Portrait | Pad rotates; exits restore previous rotation |
| Universal remote Custom IR | `kremote_*.ir` opens in Custom IR / Flipper-compatible |
| Lite env + universal remote | `m5stack-cardputer-lite` still links kvxputer universal remote |
| `menu/others` → LLM Chat | Hardware-missing message unless `HAS_LLM_MODULE` |
| SD missing | SD error handling (no hard-lock) |
| Flash size | Full `m5stack-cardputer`; use `m5stack-cardputer-lite` if oversize |
| Legacy SD paths | First boot migrates `/Bruce*` → `/support_files/*` |

## Lite env

`tools/porting/boards/m5stack-cardputer/m5stack-cardputer.ini` sibling env `m5stack-cardputer-lite` disables `EVIL_EXTENSIONS` and enables `LITE_VERSION` when flash is tight.
