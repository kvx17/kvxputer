# RFID menu domain

**RFID** (this menu) and **NFC** are separate channels:

| Menu | Modules (`config`) | Typical tools |
|------|--------------------|---------------|
| **RFID** | Unit RFID2, RC522 SPI (`rfidModule`) | Read / scan / load / erase, 125 kHz |
| **NFC** | PN532, ST25R3916 (`nfcModule`) | NDEF, emulate, EMV, companions |

Select the reader under **RFID → Config → RFID Module**. On the device, **RFID → Config → Wiring help** lists RFID2 / RC522 pinouts. PN532 / ST25 setup lives under **NFC → Config**. Full docs: [main README NFC/RFID section](../../../README.md#nfc--rfid).

## Capabilities (RFID2 / RC522)

| | RFID2 / RC522 |
|---|---|
| Classic / UL / NTAG read-write | Yes |
| Clone UID (magic) | Yes |
| Emulate tag / Emulate NDEF | No |

PN532 / ST25 capabilities are documented under the NFC channel.

## Support files

- `/support_files/rfid/` — HF dumps, `hf/`, `lf/`, `scans/`, `srix/`, `amiibo/`
- `/support_files/rfid2/` — when using M5 RFID2 module
