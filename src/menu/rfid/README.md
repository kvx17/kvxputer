# RFID menu domain

Wired HF drivers: **PN532**, **RFID2** (M5 Unit / RC522 SPI), **ST25R3916** (non-lite). Companion apps (Chameleon, PN532 BLE, PN532 UART/Killer, Amiibolink, 125 kHz) are separate menu entries, not `RFIDModules` values.

Select the reader under **RFID → Config → RFID Module**. On the device, **RFID → Config → Wiring help** lists modules, I2C/SPI mode switches, and Grove/SPI pinouts. Elechouse PN532 V3 and ITEAD/Katranji PN532 boards use **PN532 on I2C** or **PN532 on SPI** (same driver). Full docs: [main README NFC/RFID section](../../../README.md#nfc--rfid).

## Capabilities

| | PN532 | RFID2 / RC522 | ST25R3916 |
|---|---|---|---|
| Classic / UL / NTAG read-write | Yes | Yes | Yes |
| Clone UID (magic) | Yes | Yes | Yes |
| Emulate tag / Emulate NDEF | Yes | No | Yes |
| FeliCa (limited) | Yes | No | Yes |

PN532 emulate caveats: Classic dumps → NDEF as Type 4 Tag (not byte-perfect Classic); FeliCa → IDm/PMm only.

## Support files

- `/support_files/rfid/` — HF dumps, `hf/`, `lf/`, `scans/`, `srix/`, `amiibo/`
- `/support_files/rfid2/` — when using M5 RFID2 module
