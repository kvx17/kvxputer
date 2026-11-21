# Port waves (unique tools only)

Derived from **rewrite** / **onboard** rows in [AUDIT.md](AUDIT.md) that are **not** **os**.

The AppStore **shell** lives in [`app/`](app/) (`kvxputerzero`). Wave work fills channel screens inside that package — do not invent new Store package names per tool.

Stock Cardputer Zero already ships Launcher, Files, Store, FactoryTest, Camera, Compass, IR-Remote, IR-Chat, Music, Piano, Calendar, Calculator, plus Cap-LoRa-1262-GPS, Cap-CC1101-SubG-Chat, Cap-CC1101-NFC, and RFID. Prefer not to clone those domains **except** the kvxputerzero **NFC** channel (ADV NFC on Cap ST25 / optional PN532) and the **GPS** channel (ADV wardrive / survey / tracker / config on gpsd or `/dev/serial0`). Also skip OS Wi-Fi connect/AP/disconnect and launcher Setting/SSH/battery/clock.

**Focus:** network / RF-adjacent tools, scanners, specialized IR, offline crypto, BLE detectors, engineering calculators — not a second Files or Calculator.

**Shipped in `app/`:** Scientific Calculator (+ PDA Calc tile), Codec/Hash, Engineering Calculators, Passgen, TOTP, Combo, QRCodes, Barcode, Timer, **PDA**, Media Player; Clock face + Charge channel; Files browser; Infrared Use/Learn/Browse/Read/Favorites (NEC/LIRC) plus TV-B-Gone, TagTinker ESL (LED/ping/refresh/page-flip), IR Carrier check; Config colors/brightness/dim/sound/power/About; Wi-Fi Connect/Disconnect/AP info + **Monitor adapter** / **WiFi Analyzer** (USB monitor NIC); Tools leftovers (DTMF dialer/listen, Morse, Microphone, MQTT, LLM HTTP, Drone ID BLE); Scripts allowlist runner + **PC Connect** channel (how-to / needs / browse / export host scripts for Cardputer+kvxputer); **GPS** (ADV menu: Wardriving Wi-Fi/BLE/Both + WiGLE CSV + View Captures + WD Config + Master stub; Site Survey; GPS Tracker GPX; Config source/path/baud/Status); Wi-Fi/NetOps/Ethernet shared tools (Net Toolkit, Scan Hosts, TCP/Telnet/SOCKS4, File Share, Dead Drop, LAN Watch, PineAP Hunter, open networks, AP detail, Roku, WireGuard, Printer, UPnP list, HoneyPot); offline Loot Console / Check Handshakes / NTLM Clean; read-only SMB Status / SIP OPTIONS / LDAP Read; BLE passive suite + Ninebot/Amiibolink/PN532/Chameleon status (connect/read only); **Cap channels** — RF (CC1101 status/spectrum/listen/record/library/test TX), NRF24 (status/spectrum/listen/send), LoRa SX1262 (status/chat/antenna/freq/user + Meshtastic serial Nodes/Chat when USB CDC PhoneAPI or configured TEXTMSG answers, else one status row), RFID (UID read/scan/load/save), **NFC** (ADV menu: Read/Scan/Load/Erase, Classic, Tags, NDEF, Lab, Parsers, Companions, Config on Cap ST25 + optional PN532 I2C; Flipper `.nfc` / Bruce `.rfid`; honest stubs where ADV or the Cap cannot), FM Si4713 (status/broadcast/stop/spectrum); Modules Cap Probe, PaHub scan, iButton, PN532Killer UART, UART LLM; Discovery index. `meshtasticd` TCP stays out of this package. Attack / ESP-radio / USB-gadget / jammer / MouseJack leaves stay out (see AUDIT / Leave out below).

## Shared constraints (every wave)

1. Template-style arm64 `.deb` ([CardputerZero/Template](https://github.com/CardputerZero/Template)): LVGL 9.5 @ 320×170, `/dev/fb0` + evdev, cross-compile `cp0-cross`.
2. APPLaunch `.desktop`; optional `czdev` → [packages](https://github.com/CardputerZero/packages).
3. External apps only — do not fork [launcher](https://github.com/CardputerZero/launcher).
4. No designs for blocked ESP radio / USB-gadget leaves.

---

## Wave 1 — Offline / local tools (no radio bring-up)

| Candidate | Why it is not OS | Audit sources |
| --- | --- | --- |
| Engineering Calculators suite | Beyond stock Calculator (subnet, resistor, CRC, Ohm, …) | `calc/calc_menu.cpp` |
| Timer | Not launcher clock | Clock → Timer |
| PDA Notes / Memos / To-Do / Contacts / Alarms / World Clock | Calendar is stock; these extras are tools | `pda/*` (not Calendar) |
| QRCodes / Barcode | No stock QR/barcode app | `qrcode_menu`, `barcode` |
| Passgen / TOTP / Combo | Security helpers | Tools |
| Codec / Hash | Base64 / Hex / URL / MD5 / SHA | `codec` |
| Loot Console / View Captures / Check Handshakes / Aircrack offline / WiFi Pass Recovery / NTLM crack-clean | Offline FS/CPU tools | NetOps / WiFi / GPS WD viewer |
| Scripts runner | BJS or host scripts — not Store | Scripts |
| PC Connect host scripts | Browse/export bundled `PCConnect/` Python host; device USB protocol stays Cardputer-only | Scripts + PC Connect |

---

## Wave 2 — Onboard hardware tools (unique vs stock)

Stock IR-Remote / Music cover remotes and playback. These are **extra** tools:

| Candidate | Linux path | Notes |
| --- | --- | --- |
| TV-B-Gone | IR TX | Blast tool; not IR-Remote |
| IR Jammer | IR TX carrier | Specialized |
| TagTinker ESL | IR TX | ESL tool; not IR-Chat |
| DTMF / Morse / Mic Spectrum+Record | ALSA / mic | Not Music/Piano |
| UART Shell / Bus Scan / I2C Finder | Grove/EXT UART & I2C | Lab tools |
| Ethernet Scan Hosts / DHCP Starvation / MAC Flooding | Onboard Ethernet SR9900A | NIC tools; not Files |

Do not port: Media Player, Charge, Univ Rem / Custom IR / IR Read, Cap RF/LoRa/RFID domains (NFC is ported in-package).

---

## Wave 3 — Network & BLE tools

Ordinary sockets / SoftAP **tool UIs** / BlueZ **scan**. Not “Connect to WiFi” or stock SSH.

| Cluster | Candidates |
| --- | --- |
| HTTP / portals | Evil Portal, File Share, Dead Drop, WebUI, Web Crawler, CCTV, Printer |
| TCP / tunnels | Listen TCP, Client TCP, SOCKS4, Telnet, HoneyPot, Reverse Shell, Reverse TCP Tunnel |
| LAN / discovery tools | Scan Hosts, LAN Watch, PineAP Hunter, Open Wifi Checker, Net Toolkit, Roku Remote |
| Name / DHCP tools | Responder, mDNS Poison, SSDP, Switch DNS, WPAD, Autodiscover, UPnP, SMB Enum, SIP, LDAP, IPv6 RA, Rogue DHCP, DHCP Starvation, CIW, Hijack chain |
| VPN / messaging | WireGuard, MQTT, LLM HTTP stream |
| SoftAP tool | Karma Spear (SSID rotate only — not promiscuous Karma Attack) |
| BLE passive tools | BLE Scan, BLE Hunter, BLE Sniffer UI, Wall Of Flipper, Wall Of Airtag, Skimmer Detector, Ninebot, Drone ID BLE, Amiibolink/PN532-BLE/Chameleon clients |

**Excluded as os:** Connect STA, Start AP, Turn Off WiFi, AP info, SSH.

**Still out of scope (blocked):** Karma Attack, Beacon/Deauth, Sniffer, CSI, ESP-NOW, BLE Spam/HID gadget, etc.

---

## Port status

Waves 1–3, everyday apps, gap tools, and Cap/Grove accessory channels are implemented inside `app/` via `tool_registry` + `tool_screens` + `cap_screens` + `nfc_screens`. Caps are driven over Linux `spidev` / I2C / UART when present; desktop builds report Cap not present without hanging. Stock Cap Store apps may still be installed but are not required.

Still intentionally out: Evil Portal / poison / starvation / wordlist password recovery / reverse-shell class leaves; IR Jammer multi-mode and TagTinker raw-hex; Amiibo/PN532/Chameleon BLE write/emulate; full RFAL Classic Crypto1 / ISO15693 block R/W / Cap listen-IRQ MFKey; RF/NRF jammer and bruteforce; NRF MouseJack HID; Ninebot tune writes; Ethernet MAC Flooding / DHCP Starvation; Karma Spear; FM TA hijack; companion `.bin`. Stock Calendar / Music / Piano / Recorder / launcher Setting shell are not duplicated beyond what PDA and Config already cover.

---

## Out of wave scope

### os (stock — do not port)

Launcher, Files, Store, FactoryTest, Camera, Compass, IR-Remote, IR-Chat, Music, Piano, Recorder, Calendar, Calculator, Cap-LoRa-1262-GPS (stock GPS Tracker app; kvxputerzero GPS channel is the in-package ADV port), Cap-CC1101-SubG-Chat, Cap-CC1101-NFC (optional; kvxputerzero NFC channel is the in-package port), RFID; OS Wi-Fi connect/AP/info; launcher SSH / Setting / battery / clock; kvxputer RF/LoRa/RFID/Univ Rem/Media Player/Charge/Config Setting overlaps marked **os** in AUDIT (NFC and GPS are rewrite in AUDIT).

### Blocked (ESP radio / USB gadget)

| Dependency | Example leaves |
| --- | --- |
| `esp_wifi` promiscuous / `wifiRawTx` / `esp_wifi_internal_tx` | Sniffer, Jam Detect, Deauth IDS, Karma Attack, Beacon SPAM, Deauth*, Probe Attack/Sniff, PMKID, EAP Identity Sniff, Kvxgotchi, SkyJack, NetCut, CSI RSSI hop, Drone ID WiFi |
| `esp_wifi_set_csi` | CSI Radar STA CSI |
| ESP-NOW | Handshake Master, EvilChatMesh, CSI ESP-NOW, Wardriving Master, Files Send/Recv File |
| NimBLE ADV spoof / HID | BLE Spam, FindMyEvil, Name Flood, iBeacon, BLE Suite, kvxkeyboard HID, Bad BLE |
| ESP USB HID/CDC/MSC/Host | BadUSB, USB Keyboard/Clicker/U2F, Mass Storage, BadUSB Hunter |
| `esp_wifi_set_mac` | Change MAC |

### Accessory (not first waves; stock Caps often cover)

NRF24 tools, Si4713 FM, Grove Scroll/Joystick/PaHub, OneWire iButton, PN532Killer UART. Prefer stock Cap/RFID apps over reimplementing Sub-GHz/LoRa radio domains (NFC and GPS channels are already in-package ports).

### Drop

ESP32C5 Serial, PC Connect device protocol, Deep Sleep, Toggle BLE API, companion `.bin` list, ESP JS App Store install.

### Assets

themes, `.ir`, scripts, wordlists, portals — reuse later for tool apps; companion `.bin` are not Zero packages.
