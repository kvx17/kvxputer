# PC Connect

**PC Connect** lets you drive kvxputer tools from a computer over a single USB cable. The Cardputer does the radio work (WiFi, BLE, IR, Sub-GHz, RFID, NFC, GPIO); the PC shows live tables, saves logs, and gives you a bigger screen and keyboard.

There are two halves:

| Half | Where | What it does |
| ---- | ----- | ------------ |
| **PC Connect app** | On the Cardputer: home tile **PC Connect**, or **USB → PC Connect** | Waits for the PC, runs the requested radio tool, streams results back over USB serial. Shows link status, active radio, and events/second. **Esc** leaves. |
| **`pcconnect.py`** | This folder, on your PC | Finds the Cardputer, opens one USB session, and gives you a menu of tools. |

Not available in the **lite** build.

---

## Requirements

- **Linux or macOS** with Python 3.8+. Windows is not supported natively (the script uses `termios` for the serial port and keyboard).
- `pyserial` (`pip install -r requirements.txt`).
- A **data-capable** USB cable.
- Linux: your user in the `uucp` (Arch) or `dialout` (Debian/Ubuntu) group so you can open `/dev/ttyACM*`. Log out and back in after adding the group.
- No other program holding the port (close `pio device monitor`, Arduino serial monitor, etc.).

---

## Getting the scripts onto a PC

You can clone the repo, **or** carry the scripts on the Cardputer itself:

1. Copy this whole `PCConnect/` folder onto the microSD card (see [SD card](../README.md#sd-card)).
2. On any PC, plug in the Cardputer and open **USB → Mass Storage** (also under **Files**). The SD card shows up as a USB drive.
3. Copy the `PCConnect` folder from the drive to the PC, then eject the drive and leave Mass Storage on the Cardputer.
4. Continue with **Quick start** below.

Mass Storage and PC Connect both use the USB port, so only one can run at a time: copy first, then switch to PC Connect.

---

## Quick start

```bash
cd PCConnect
python3 -m pip install -r requirements.txt
```

1. On the Cardputer open **PC Connect** (home grid, or **USB → PC Connect**). The screen shows `Link: waiting`.
2. On the PC run:

   ```bash
   python3 pcconnect.py
   ```

3. The script scans serial ports (Espressif USB `303a:*` / `ttyACM*` first), says hello, and auto-selects the device if only one answers. With several devices it lists them and asks for a number.
4. The Cardputer switches to `Link: yes`. Pick a tool from the numbered menu on the PC.
5. **q** returns to the menu / quits. Pressing **Esc** on the Cardputer ends the session on both sides.

Skip discovery and use a known port:

```bash
python3 pcconnect.py --port /dev/ttyACM0      # Linux
python3 pcconnect.py --port /dev/cu.usbmodem1101   # macOS
```

If the Cardputer is plugged in but the PC Connect app is not open yet, the script waits and connects as soon as you open it.

---

## Tools

| Menu entry | Needs | What you get on the PC |
| ---------- | ----- | ---------------------- |
| **kvx wifi analyzer** | — | Per-channel airtime bars (load %, peak %, RSSI) plus a live AP list (channel, SSID, RSSI, auth, BSSID). Saves a CSV to `logs/`. |
| **BLE Scan** | — | Live BLE advertiser table. |
| **Wall of Flipper** *(ext)* | — | Flipper Zero devices seen over BLE. |
| **Wall of Airtag** *(ext)* | — | AirTag / Find My trackers nearby. |
| **Skimmer Detector** *(ext)* | — | Suspicious BLE skimmer modules. |
| **GPIO** | — | Interactive console: `read`, `mode`, `set` on allowed pins. |
| **IR Receive** | IR RX | Decoded IR signals as they arrive; can also **send** signals from a Flipper-style `.ir` file on the PC. |
| **Sub-GHz RX** | RF433R or CC1101 | Captured remote signals. Keys: **f** change frequency, **r** raw mode, **t** replay the last capture. |
| **Sub-GHz RSSI** | CC1101 | Live signal strength at the configured frequency. |
| **RFID Read** | RFID2 / RC522 | Tag UID and type. |
| **NFC Toolkit** | PN532 / M5 Unit NFC | Full NFC menu (below). |
| **Jam Detect** | — | WiFi jamming / noise heuristics in real time. |

*(ext)* entries only appear on builds with `EVIL_EXTENSIONS` (Cardputer full build). The PC only lists tools the firmware reports, so the menu always matches what your device can do.

Modules (RFID reader, NFC reader, RF module, IR pins) are picked on the Cardputer in each app's **Config**, exactly as when using the tools on-device.

### NFC Toolkit

The Cardputer handles the radio; the PC shows dumps, files and status.

| # | Option | Purpose |
| - | ------ | ------- |
| 1 | Live read | Continuous read; each tag shows up as it is presented. |
| 2 | Read once | Single read with full dump. |
| 3 | Emulate NDEF | Type a URL or text on the PC and the Cardputer emulates it as an NDEF tag. Tap a phone/reader; **q** / Esc stops. |
| 4 | Emulate loaded dump | Emulate a dump file from the SD card. |
| 5 | Write / Clone / Erase / NDEF write | Write operations on a blank or writable tag. |
| 6 | Recorded files | Browse `/support_files/rfid/` (hf, scans, amiibo, nonces, apdu). |
| 7 | Lab | APDU scripts, compare file vs live tag, field presence. |
| 8 | Classic | Dictionary attack and magic-card helpers. |
| 9 | Protocols | ISO15693, DESFire, FeliCa and other protocol tools. |

---

## Logs

Results are written next to the script in `logs/`:

- `wifi.analyzer-<timestamp>.csv` and other per-session CSV files from live tables.
- `<app>.jsonl` event logs (for example `nfc.read.jsonl`).

---

## Troubleshooting

`No kvxputer device answered hello` — work through the checklist the script prints:

1. Firmware is current; `lsusb` shows **Espressif 303a:1001**.
2. The cable carries data (many charge cables don't) and you are in `uucp` / `dialout`.
3. Nothing else is holding `/dev/ttyACM*`.
4. **PC Connect** is open on the Cardputer.

Other messages:

- `Device left PC Connect` — Esc was pressed on the Cardputer. Open PC Connect again and rerun the script.
- `USB connection lost` — cable unplugged or the device rebooted. Reopen PC Connect, then rerun.
- `radio busy` / `start failed` / `module not found` / `CC1101 required` — the tool needs hardware that isn't configured. Check the matching **Config** menu on the Cardputer.
