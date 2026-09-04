# SD pack

Copy this folder onto a Cardputer microSD so large assets and companion images are available at runtime.

On the card, the tree must be:

```text
/support_files/
  companions/     *.bin for extra ESP boards (C5 slave, CSI beacon, …)
  wifi/
  netops/
  infrared/esl/   ESL bitmaps (optional)
  ble/
```

Firmware looks up the same paths on SD first, then LittleFS. LittleFS only ships a small factory seed (`tools/build/data/support_files/`).

## Companions

Build with PlatformIO (does not use `resources/`):

```bash
pio run -e companion-c5-unified
pio run -e companion-csi-beacon
pio run -e companion-findmy
pio run -e companion-chatmesh-relay
```

`tools/build/copy_companions.py` copies resulting `.bin` files here after those envs build. Flash companions from a PC (`pio run -e companion-* -t upload`). On the Cardputer: **Modules → Companion bins**.
