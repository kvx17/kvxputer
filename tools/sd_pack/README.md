# SD pack

Copy the **contents** of this folder onto a Cardputer microSD root so runtime assets match firmware paths (`kvx::paths`). Firmware checks SD first, then LittleFS.

```text
/support_files/
  companions/          companion *.bin (build companion-* envs)
  wifi/                portals, wordlists, deaddrop, probes, wof, …
  evil portals/        extra captive-portal HTML
  netops/              ciw, cctv, crawler, printer, responder/NTLM,
                       toolkit/ (port lists top20/top100, hostnames), mdns/ (spoof names)
  rf/library/          sample .sub presets for RF → Protocol Library
  mqtt/                default topics for Tools → MQTT
  infrared/            Flipper-IRDB library, profiles/, remotes/, esl/
  badusb/              Rubber Ducky, Bash Bunny, O.MG, Flipper, kvx17 payloads
  ble/                 names, FindMy keys, airtags
  rfid/                sample .rfid dumps
  media/               audio + images
  others/              misc helpers
/root/themes/          UI themes (factory boot GIF is firmware-embedded, not here)
/menu/scripts/         interpreter samples
/PCConnect/            optional, copied from the repo root (see below)
```

## Recommended: also copy `PCConnect/`

Copy the repo's [`PCConnect/`](../../PCConnect/README.md) folder to the SD root as `/PCConnect/`. The Cardputer then carries its own PC-side scripts, so you can use **PC Connect** from any Linux/macOS computer without cloning the repo:

1. Plug the Cardputer into the PC and open **USB → Mass Storage**. The SD card shows up as a USB drive.
2. Copy `PCConnect/` from the drive to the PC, eject, and exit Mass Storage.
3. On the Cardputer open **PC Connect**; on the PC run `pip install pyserial` once, then `python3 pcconnect.py`.

Mass Storage and PC Connect share the USB port, so copy first and then switch apps.

```text
/PCConnect/
  pcconnect.py         entry point
  protocol.py          USB serial protocol
  apps/                one script per tool (wifi, ble, ir, rf, rfid, nfc, gpio, jam)
  requirements.txt     pyserial
  logs/                CSV / JSONL output (created on the PC copy)
```

Skip `__pycache__/` and old files in `logs/` when copying.

## Other paths

Sample Sub-GHz presets live in `support_files/rf/library/` (gate, car fob, doorbell) and show up under **RF → Protocol Library**.

On-device (not copied from this pack): `/kvxputer/scripts/badUSB` (BadUSB editor), `/kvxputer/infrared/remotes` (kremote), `/kvxputer/kvxuniversalremote/userSettings.json` (kremote favorites).

Populated from (copied, not moved):

| Source | Mapped into pack |
|--------|------------------|
| `tools/build/data/support_files/` | base seed (same layout) |
| `resources/sd_files/` | IR → `infrared/profiles`, nfc → `rfid`, portals, themes, BadUSB, interpreter |
| `resources/Evil-M5Project-main/SD-Card-File/` | wifi/netops/ble/esl/media/portals/BadUSB |

Omitted on purpose (too large for the repo): Evil deaddrop `phrackFULL.tar` (~24 MB) and its large mp3. Copy those onto the card yourself if you need them.

`resources/` stays gitignored local reference only.

## Companions

Build with PlatformIO (does not use `resources/`):

```bash
pio run -e companion-c5-unified
pio run -e companion-csi-beacon
pio run -e companion-findmy
pio run -e companion-chatmesh-relay
pio run -e companion-c5-802154
```

`tools/build/copy_companions.py` copies resulting `.bin` files into `support_files/companions/` after those envs build. Flash companions from a PC (`pio run -e companion-* -t upload`). On the Cardputer: **Modules → Companions**.
