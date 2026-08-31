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
| `menu/netops` carousel tile | Present after WiFi; tools + extras stubs |
| `menu/ble` → BLE Keyboard + extras | HID works; extras show pending message |
| SD missing | SD error handling (no hard-lock) |
| Flash size | Full `m5stack-cardputer`; use `m5stack-cardputer-lite` if oversize |
| Legacy SD paths | First boot migrates `/Bruce*` → `/support_files/*` |

## Lite env

`tools/porting/boards/m5stack-cardputer/m5stack-cardputer.ini` sibling env `m5stack-cardputer-lite` disables `EVIL_EXTENSIONS` and enables `LITE_VERSION` when flash is tight.
