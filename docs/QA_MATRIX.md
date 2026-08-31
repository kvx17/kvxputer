# QA matrix (kvxputer Cardputer ADV)

Source domains: `src/menu/<id>/` — see [ARCHITECTURE.md](ARCHITECTURE.md).

| Scenario | Expect |
|----------|--------|
| Boot without Unit Scroll | Silent; keyboard carousel works |
| Boot with Unit Scroll on PORT.A | Dim green LED; CW/CCW navigates; press selects |
| Config → Unit Scroll → Reconnect | Status updates; success/fail message |
| Unit Scroll unplug mid-session | Next poll marks absent; keyboard still works |
| Grove RF (CC1101) + Scroll | Status shows Grove busy; prefer one accessory |
| `menu/wifi` → Evil Portal / Wifi Atks | P0 WiFi path works |
| `menu/wifi` → Probes / Handshakes / Wall Of Flipper / Dead Drop / Open Wifi / Aircrack / CSI Radar / C5 Serial | Addon menus open (`EVIL_EXTENSIONS`) |
| `menu/netops` carousel tile | Present after WiFi; Bruce rows + P1/P2 addon menus |
| `menu/ble` → HID Remote + Name Flood / AirTag / FindMy | HID works; addons open |
| `menu/gps` → Wardriving → Wardriving Master | Addon under existing Wardriving submenu |
| `menu/infrared` → TagTinker ESL | Addon listed with other IR tools |
| `menu/others` → LLM Chat | Hardware-missing message unless `HAS_LLM_MODULE` |
| SD missing | SD error handling (no hard-lock) |
| Flash size | Full `m5stack-cardputer`; use `m5stack-cardputer-lite` if oversize |
| Legacy SD paths | First boot migrates `/Bruce*` → `/support_files/*` |

## Lite env

`tools/porting/boards/m5stack-cardputer/m5stack-cardputer.ini` sibling env `m5stack-cardputer-lite` disables `EVIL_EXTENSIONS` and enables `LITE_VERSION` when flash is tight.
