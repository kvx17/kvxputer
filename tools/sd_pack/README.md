# SD pack

Copy the **contents** of this folder onto a Cardputer microSD root so runtime assets match firmware paths (`kvx::paths`). Firmware checks SD first, then LittleFS.

```text
/support_files/
  companions/          companion *.bin (build companion-* envs)
  wifi/                portals, wordlists, deaddrop, probes, wof, …
  netops/              ciw, cctv, crawler, printer, responder/NTLM
  infrared/
    profiles/          Flipper-style .ir library (TVs, ACs, …)
    remotes/           kremote learned profiles
    esl/               ESL bitmaps
  ble/                 names, FindMy keys, airtags
  rfid/                sample .rfid dumps
  media/               audio + images
  others/              misc helpers
/root/themes/          UI themes
/menu/scripts/         BadUSB / interpreter samples
```

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
```

`tools/build/copy_companions.py` copies resulting `.bin` files into `support_files/companions/` after those envs build. Flash companions from a PC (`pio run -e companion-* -t upload`). On the Cardputer: **Modules → Companion bins**.
