# Companion firmware

Standalone sketches for extra ESP chips. **Not** compiled into Cardputer firmware.

| Env | Hardware | Role |
|-----|----------|------|
| `companion-csi-beacon` | ESP32-S3 (or similar) | CSI / ESP-NOW beacon for CSI Radar |
| `companion-findmy` | ESP32-S3 | FindMy advertiser |
| `companion-chatmesh-relay` | ESP32 | EvilChatMesh relay |
| `companion-c5-unified` | ESP32-C5 | UART slave for WiFi → ESP32C5 Serial |

```bash
pio run -e companion-csi-beacon
pio run -e companion-c5-unified -t upload
```

Binaries are copied to `tools/sd_pack/support_files/companions/`. Put them on the Cardputer SD card at `/support_files/companions/`. Source was extracted from Evil-M5Project slave sketches (7h30th3r0n3); firmware does not read `resources/`.
