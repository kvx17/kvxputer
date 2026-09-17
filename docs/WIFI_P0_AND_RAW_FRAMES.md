# WiFi P0 and raw 802.11 frames

## P0 coverage (Bruce)

Cardputer ADV uses Bruce `WifiMenu` for:

- Connect / AP / MAC (`wifi_common`, WiFi Config)
- Wifi Atks (`modules/wifi/wifi_atks`)
- Evil Portal (`modules/wifi/evil_portal`)
- Sniffer / Channel Analyzer
- Karma (`modules/wifi/karma_attack`)

No Evil monolith code is required for the P0 path.

## Raw frame / ESP32 core patches

Evil-Cardputer overrides `ieee80211_raw_frame_sanity_check` and documents Arduino-core patches under Evil `utilities/compilation_prerequisites`.

**kvxputer default:** use Bruce’s existing WiFi attack / sniffer stack. Do **not** apply Evil’s core patch unless a specific Evil port fails without it.

When a ported tool needs raw TX:

1. Confirm Bruce `wifi_atks` / sniffer already work on the installed ESP32 Arduino core.
2. Only then evaluate Evil’s sanity-check override / linker wrap.
3. Document the required PlatformIO / sdkconfig change in this file before merging.

## Evil WiFi extras

Menu: **WiFi → Evil WiFi Extras** (`-DEVIL_EXTENSIONS=1`).  
Stubs for Handshake Master, Wall of Flipper, probe CRUD, etc. Full ports land behind the same flag.
