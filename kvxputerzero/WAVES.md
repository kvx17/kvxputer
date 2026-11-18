# Port waves (unique tools only)

Derived from **rewrite** / **onboard** rows in [AUDIT.md](AUDIT.md) that are **not** **os**.

The AppStore **shell** lives in [`app/`](app/) (`kvxputerzero`). Wave work fills channel screens inside that package — do not invent new Store package names per tool.

Stock Cardputer Zero already ships Launcher, Files, Store, FactoryTest, Camera, Compass, IR-Remote, IR-Chat, Music, Piano, Calendar, Calculator, plus Cap-LoRa-1262-GPS, Cap-CC1101-SubG-Chat, Cap-CC1101-NFC, and RFID. Do **not** port those domains. Also skip OS Wi-Fi connect/AP/disconnect and launcher Setting/SSH/battery/clock.

**Focus:** network / RF-adjacent tools, scanners, specialized IR, offline crypto, BLE detectors, engineering calculators — not a second Files or Calculator.

**MVP shipped in `app/`:** Codec/Hash, Engineering Calculators (full suite), Passgen, TOTP, Combo, QRCodes, Barcode, Timer, **PDA** (hub + Notes/Memos/To-Do/Calendar/Contacts/Alarms/World Clock faces), Config colors, channel placeholders.

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

Do not port: Media Player, Charge, Univ Rem / Custom IR / IR Read, Cap RF/LoRa/NFC/RFID domains.

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

## Suggested order for the later port plan

1. Wave 1 engineering calculators or TOTP/Codec (prove Template → `.deb`).
2. Wave 1 QR/Barcode or PDA Notes.
3. Wave 3 Net Toolkit or Roku (sockets).
4. Wave 3 File Share / Evil Portal class HTTP tools.
5. Wave 3 BLE Scan / Wall Of Flipper / Skimmer Detector.
6. Wave 2 TV-B-Gone or TagTinker (IR extras).
7. Wave 2 Mic/DTMF or Ethernet Scan Hosts.
8. Wave 1 Scripts runner (larger).

No Wave for Calculator, Files, Calendar, Music, IR-Remote, Cap-LoRa/CC1101/NFC, RFID, Wi-Fi connect UI, or Setting shell.

---

## Out of wave scope

### os (stock — do not port)

Launcher, Files, Store, FactoryTest, Camera, Compass, IR-Remote, IR-Chat, Music, Piano, Recorder, Calendar, Calculator, Cap-LoRa-1262-GPS, Cap-CC1101-SubG-Chat, Cap-CC1101-NFC, RFID; OS Wi-Fi connect/AP/info; launcher SSH / Setting / battery / clock; kvxputer RF/LoRa/GPS Tracker/RFID/NFC/Univ Rem/Media Player/Charge/Config Setting overlaps marked **os** in AUDIT.

### Blocked (ESP radio / USB gadget)

| Dependency | Example leaves |
| --- | --- |
| `esp_wifi` promiscuous / `wifiRawTx` / `esp_wifi_internal_tx` | Sniffer, analyzer, Jam Detect, Deauth IDS, Karma Attack, Beacon SPAM, Deauth*, Probe Attack/Sniff, PMKID, EAP Identity Sniff, Kvxgotchi, SkyJack, NetCut, CSI RSSI hop, Drone ID WiFi |
| `esp_wifi_set_csi` | CSI Radar STA CSI |
| ESP-NOW | Handshake Master, EvilChatMesh, CSI ESP-NOW, Wardriving Master, Files Send/Recv File |
| NimBLE ADV spoof / HID | BLE Spam, FindMyEvil, Name Flood, iBeacon, BLE Suite, kvxkeyboard HID, Bad BLE |
| ESP USB HID/CDC/MSC/Host | BadUSB, USB Keyboard/Clicker/U2F, Mass Storage, BadUSB Hunter |
| `esp_wifi_set_mac` | Change MAC |

### Accessory (not first waves; stock Caps often cover)

NRF24 tools, Si4713 FM, Grove Scroll/Joystick/PaHub, OneWire iButton, PN532Killer UART, wardrive **with** GPS Cap (GPS Tracker is **os**). Prefer stock Cap/RFID apps over reimplementing Sub-GHz/NFC/LoRa/GPS.

### Drop

ESP32C5 Serial, PC Connect device protocol, Deep Sleep, Toggle BLE API, companion `.bin` list, ESP JS App Store install.

### Assets

themes, `.ir`, scripts, wordlists, portals — reuse later for tool apps; companion `.bin` are not Zero packages.
