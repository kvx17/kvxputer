# Cardputer Zero — app portability audit

Catalog: full non-lite `m5stack-cardputer` (`EVIL_EXTENSIONS=1`, `HAS_NS4168_SPKR`, `MIC_SPM1423`, `USB_as_HID`, `USE_CC1101_VIA_SPI`, `HAS_LORA_CAP`, `FM_SI4713`). Channels from [`src/root/ui/menu_registry.cpp`](../src/root/ui/menu_registry.cpp).

**Scope:** Prefer unique **tools**. Leaves already covered by Cardputer Zero stock apps or OS networking are **os** (do not port). See [README.md](README.md) built-in table.

**Lite-SKU (C155):** `no` unless the leaf needs camera or IMU/magnetometer (none of the kvxputer leaves do). Cap/Grove accessories are separate from the Lite/full Zero SKU.

**Role:** `tool` = user-facing app; `setting` = preference / pin / config leaf.

**Discovery** leaves that only launch another channel’s app are listed once in their home channel; the Discovery section is a cross-reference and is **not** double-counted in the totals.

## Counts (leaf tools + settings)

| Verdict | Count (approx.) | Notes |
| --- | ---: | --- |
| rewrite | ~70 | Unique tools: NetOps sockets, offline crypto, specialized UI, BlueZ scan tools |
| onboard | ~20 | Unique onboard tools (IR extras, mic/DTMF/Morse, Ethernet NIC tools, UART/I2C) |
| os | ~45 | Stock Zero apps / launcher / OS Wi-Fi — **do not port** (see README built-in table) |
| accessory | ~90 | Caps already exist for LoRa/GPS/CC1101/NFC/RFID; Grove extras remain accessory |
| blocked | ~75 | `esp_wifi` promiscuous/raw TX, CSI, ESP-NOW, NimBLE ADV/HID as used, ESP USB gadget/host |
| drop | ~8 | PC Connect CDC, ESP32-C5 UART bridge, ESP sleep / firmware App Store |
| asset | (separate) | `tools/sd_pack/`, `PCConnect/` host tree — not in tool counts |

Waves use only **rewrite** / **onboard** that are not **os**. Submenu headers (`Back`, `Main Menu`) are omitted.

### OS coverage map (quick)

| Zero stock | kvxputer leaves marked **os** |
| --- | --- |
| Files | SD Card, LittleFS browsers |
| Calculator / launcher Math | Calculator |
| Calendar | PDA Calendar / Add Event |
| Music / Piano / Recorder | Media Player |
| IR-Remote (+ IR-Chat) | Univ Rem Learn/Use/Favorites/Browse/Delete, Custom IR basic, IR Read, IR HW settings |
| Cap-LoRa-1262-GPS | LoRa Chat, Meshtastic, GPS Tracker / Cap GPS source, Antenna Check |
| Cap-CC1101-* / RFID | RF Sub-GHz channel, NFC/RFID channels (stock Cap/RFID apps cover domain) |
| launcher Setting / status | Connect STA, Start AP, Turn Off WiFi, AP info, Charge screen, Clock face, brightness/theme/power Setting overlap, Restart/Power Off |
| launcher / Store SSH | SSH client |
| Store / launcher Store | Install App Store (also **drop** for ESP JS store) |
| Camera / Compass / FactoryTest | no kvxputer leaves (Zero-only) |

---

## WiFi

| Label | Source | Role | Verdict | Deciding dependency | Gate | Lite SKU |
| --- | --- | --- | --- | --- | --- | --- |
| Connect to Wifi | `wifi_menu.cpp` / `wifi_common.cpp` | tool | rewrite | Ported in kvxputerzero Wi-Fi → Connect (nmcli); launcher Wi-Fi still available | always | no |
| Start WiFi AP | `wifi_menu.cpp` / `wifi_common.cpp` | tool | os | OS SoftAP / host networking — not a kvx tool target | always | no |
| Turn Off WiFi | `wifi_menu.cpp` | tool | rewrite | Ported as Wi-Fi → Disconnect / radio off | when up | no |
| AP info | `ap_info.cpp` | tool | rewrite | Ported as Wi-Fi → AP info (`net_info`) | when STA | no |
| File Share → Create AP | `files/file_share.cpp` | tool | rewrite | SoftAP + AsyncWebServer | always | no |
| File Share → Use WiFi | `files/file_share.cpp` | tool | rewrite | STA + HTTP file share | always | no |
| File Share portal settings | `files/file_share.cpp` | setting | rewrite | prefs only | always | no |
| Karma Attack | `karma_attack.cpp` | tool | blocked | `esp_wifi` promiscuous + `wifiRawTx` | `!LITE` | no |
| Beacon SPAM | `wifi_atks.cpp` | tool | blocked | `wifiRawTx` beacons | always | no |
| Deauth Flood | `wifi_atks.cpp` | tool | blocked | `wifiRawTx` deauth | always | no |
| Target → Information | `wifi_atks.cpp` | tool | rewrite | scanned AP metadata UI | always | no |
| Target → Deauth | `deauther.cpp` | tool | blocked | `wifiRawTx` | always | no |
| Target → Capture Handshake | `wifi_atks.cpp` / sniffer | tool | blocked | promiscuous RX + optional raw TX | `!LITE` | no |
| Target → Clone Portal variants | `evil_portal.cpp` | tool | blocked | SoftAP portal + raw deauth | always | no |
| Station / All Clients / Target List Deauth | `deauther.cpp` | tool | blocked | promiscuous + `wifiRawTx` | always | no |
| Evil Portal | `evil_portal.cpp` | tool | rewrite | SoftAP captive (`DNSServer` + AsyncWebServer) | always | no |
| NetCut | `netcut.cpp` | tool | blocked | ARP inject via `esp_wifi_internal_tx` | always | no |
| Listen TCP | `tcp_utils.cpp` | tool | rewrite | `WiFiServer` TCP | `!LITE` | no |
| Client TCP | `tcp_utils.cpp` | tool | rewrite | `WiFiClient` TCP | `!LITE` | no |
| SOCKS4 Proxy | `socks4_proxy.cpp` | tool | rewrite | TCP SOCKS4 | `!LITE` | no |
| TelNET | `clients.cpp` | tool | rewrite | TCP telnet client | `!LITE` | no |
| SSH | `clients.cpp` | tool | os | launcher SSH / Store Zero SSH | `!LITE` | no |
| Sniffer | `sniffer.cpp` | tool | blocked | `esp_wifi_set_promiscuous` | `!LITE` | no |
| kvx wifi analyzer | `channel_analyzer.cpp` | tool | blocked | promiscuous hop (ESP path) | `!LITE` | no |
| Jam Detect | `jam_detect.cpp` | tool | blocked | promiscuous mgmt counters | `!LITE` | no |
| Deauth IDS → Start | `jam_detect.cpp` | tool | blocked | promiscuous deauth IDS | `!LITE` | no |
| Deauth IDS → Help | `jam_detect.cpp` | setting | rewrite | UI text | `!LITE` | no |
| PineAP Hunter → Start | `pineap_hunter.cpp` | tool | rewrite | `WiFi.scanNetworks` heuristics | `!LITE` | no |
| LAN Watch → Start | `lan_watch.cpp` | tool | rewrite | lwIP ARP gateway watch | `!LITE` | no |
| Scan Hosts | ARPScanner via `wifi_menu.cpp` | tool | rewrite | ARP scan on STA | `!LITE` | no |
| Wireguard | `root/net/wg.cpp` | tool | rewrite | WireGuard over STA | `!LITE` | no |
| Responder | `responder.cpp` | tool | rewrite | SoftAP + UDP/TCP listeners | `!LITE` | no |
| Net Toolkit (Port Scan, Dig, Ping, Curl-lite, …) | `net_toolkit.cpp` | tool | rewrite | STA sockets / DNS / HTTP | `!LITE` | no |
| Kvxgotchi → Find friends | `pwnagotchi.cpp` / `pwngrid.cpp` | tool | blocked | promiscuous + `wifiRawTx` | `!LITE` | no |
| Kvxgotchi → Pwngrid spam | `pwnagotchi/spam.cpp` | tool | blocked | `wifiRawTx` | `!LITE` | no |
| WiFi Pass Recovery | `wifi_recover.cpp` | tool | rewrite | offline CPU/FS vs pcap+wordlist | `!LITE` | no |
| Roku Remote | `roku/roku_menu.cpp` | tool | rewrite | SSDP + HTTP ECP | always | no |
| Change MAC | `wifi_mac.cpp` | setting | blocked | `esp_wifi_set_mac` as used | always | no |
| kvx Wifi Settings / portal endpoints | `wifi_menu.cpp` | setting | rewrite | portal prefs | always | no |
| Probe Attack | `probe/probe.cpp` | tool | blocked | `wifiRawTx` probes | `EVIL_EXTENSIONS` | no |
| Probe Sniffing | `probe/probe.cpp` | tool | blocked | promiscuous probe RX | `EVIL_EXTENSIONS` | no |
| Karma Spear | `probe/probe.cpp` | tool | rewrite | SoftAP SSID rotate | `EVIL_EXTENSIONS` | no |
| Probe list mgmt | `probe/probe.cpp` | setting | rewrite | FS probe list | `EVIL_EXTENSIONS` | no |
| Handshake Master | `handshake_master.cpp` | tool | blocked | ESP-NOW `esp_now_*` | `EVIL_EXTENSIONS` | no |
| Check Handshakes | `handshake_master.cpp` | tool | rewrite | FS pcap listing | `EVIL_EXTENSIONS` | no |
| PMKID → Start | `pmkid/pmkid.cpp` | tool | blocked | promiscuous EAPOL | `EVIL && !LITE` | no |
| Wall Of Flipper | `wall_of_flipper.cpp` | tool | rewrite | BLE ADV scan heuristics → BlueZ | `EVIL_EXTENSIONS` | no |
| WiFi Dead Drop | `dead_drop_menu.cpp` | tool | rewrite | SoftAP + HTTP listing | `EVIL_EXTENSIONS` | no |
| Open Wifi Checker | `open_wifi_menu.cpp` | tool | rewrite | scan + STA connect open APs | `EVIL_EXTENSIONS` | no |
| Aircrack | `aircrack_menu.cpp` | tool | rewrite | offline MIC vs wordlist | `EVIL_EXTENSIONS` | no |
| CSI Radar → RSSI hop | `csi_radar_menu.cpp` | tool | blocked | promiscuous RSSI | `EVIL_EXTENSIONS` | no |
| CSI Radar → STA CSI | `csi_radar_menu.cpp` | tool | blocked | `esp_wifi_set_csi` | `EVIL_EXTENSIONS` | no |
| CSI Radar → ESP-NOW | `csi_radar_menu.cpp` | tool | blocked | ESP-NOW | `EVIL_EXTENSIONS` | no |
| ESP32C5 Serial / 802.15.4 Sniff | `c5_serial_menu.cpp` | tool | drop | UART host to ESP32-C5 companion firmware | `EVIL_EXTENSIONS` | no |

---

## Discovery

Launcher over `app_catalog` (`discovery_menu.cpp`). Leaves are the same apps as WiFi / BLE / RF / GPS / NRF24 / NetOps / USB — see those sections. No unique implementation.

---

## NetOps

| Label | Source | Role | Verdict | Deciding dependency | Gate | Lite SKU |
| --- | --- | --- | --- | --- | --- | --- |
| SSH | shared with WiFi | tool | os | launcher / Store SSH | `!LITE` | no |
| Scan Hosts / Listen TCP / Client TCP / Responder / Net Toolkit | shared with WiFi | tool | rewrite | ordinary sockets / SoftAP tool UIs | `!LITE` | no |
| Loot Console | `loot_console.cpp` | tool | rewrite | FS loot files | `!LITE` | no |
| Reverse Shell | `reverseShell/reverseShell.cpp` | tool | rewrite | SoftAP captive + TCP | `!LITE` | no |
| Web Crawler | `web_crawler_menu.cpp` | tool | rewrite | STA HTTP | `EVIL_EXTENSIONS` | no |
| Reverse TCP Tunnel | `reverse_tcp_menu.cpp` | tool | rewrite | STA TCP tunnel | `EVIL_EXTENSIONS` | no |
| DHCP Starvation | `dhcp_starvation_menu.cpp` | tool | rewrite | UDP DHCP Discover flood | `EVIL_EXTENSIONS` | no |
| Rogue DHCP STA/AP | `rogue_dhcp_menu.cpp` | tool | rewrite | UDP DHCP server | `EVIL_EXTENSIONS` | no |
| IPv6 RA | `ipv6_ra_menu.cpp` | tool | rewrite | ICMPv6 socket | `EVIL_EXTENSIONS` | no |
| Switch DNS | `switch_dns_menu.cpp` | tool | rewrite | DNSServer wildcard | `EVIL_EXTENSIONS` | no |
| Network Hijacking | `hijack_menu.cpp` | tool | rewrite | chains sockets + SoftAP portal | `EVIL_EXTENSIONS` | no |
| mDNS Poison | `mdns_menu.cpp` | tool | rewrite | UDP `:5353` | `EVIL_EXTENSIONS` | no |
| SMB Enum | `smb_enum_menu.cpp` | tool | rewrite | TCP `:139`/`:445` | `EVIL_EXTENSIONS` | no |
| WPAD Abuse | `wpad_menu.cpp` | tool | rewrite | SoftAP + DNS + HTTP wpad | `EVIL_EXTENSIONS` | no |
| Crack / Clean NTLMv2 | `ntlm_menu.cpp` | tool | rewrite | FS/CPU hashes | `EVIL_EXTENSIONS` | no |
| UART Shell | `uart_shell_menu.cpp` | tool | onboard | `HardwareSerial` → Linux tty/UART on Grove/EXT | `EVIL_EXTENSIONS` | no |
| Printer tools | `printer_menu.cpp` | tool | rewrite | STA TCP printer ports | `EVIL_EXTENSIONS` | no |
| HoneyPot | `honeypot_menu.cpp` | tool | rewrite | TCP listeners | `EVIL_EXTENSIONS` | no |
| EvilChatMesh | `chat_mesh_menu.cpp` | tool | blocked | ESP-NOW | `EVIL_EXTENSIONS` | no |
| SIP tools | `sip_menu.cpp` | tool | rewrite | UDP SIP | `EVIL_EXTENSIONS` | no |
| CCTV Toolkit | `cctv_menu.cpp` | tool | rewrite | STA HTTP | `EVIL_EXTENSIONS` | no |
| SSDP Poisoner | `ssdp_menu.cpp` | tool | rewrite | UDP SSDP | `EVIL_EXTENSIONS` | no |
| SkyJack | `skyjack_menu.cpp` | tool | blocked | `esp_wifi_80211_tx` + TCP | `EVIL_EXTENSIONS` | no |
| UPnP tools | `upnp_menu.cpp` | tool | rewrite | SSDP + HTTP IGD | `EVIL_EXTENSIONS` | no |
| LDAP Dump | `ldap_menu.cpp` | tool | rewrite | TCP LDAP | `EVIL_EXTENSIONS` | no |
| Autodiscover Abuse | `autodiscover_menu.cpp` | tool | rewrite | SoftAP + DNS + HTTP | `EVIL_EXTENSIONS` | no |
| CIW Zeroclick | `ciw_menu.cpp` | tool | rewrite | SoftAP SSID rotation | `EVIL_EXTENSIONS` | no |
| EAP Identity Sniff | `imsi_eap_menu.cpp` | tool | blocked | `esp_wifi_set_promiscuous` | `EVIL_EXTENSIONS` | no |

---

## BLE

| Label | Source | Role | Verdict | Deciding dependency | Gate | Lite SKU |
| --- | --- | --- | --- | --- | --- | --- |
| Disconnect | `ble_menu.cpp` | tool | rewrite | stop BLE stack | `!LITE` | no |
| kvxkeyboard HID (BLE modes) | `hid_remote_*` | tool | blocked | NimBLE HID GATT as implemented | `!LITE` | no |
| HID USB path | `hid_remote_*` | tool | blocked | ESP USB HID gadget (`USB_as_HID`) | `!LITE && USB_as_HID` | no |
| HID Settings | `hid_remote_menu.cpp` | setting | blocked | NimBLE bonds/adv | `!LITE` | no |
| Media Cmds / Bad BLE / BLE Keyboard / Presenter (legacy) | `ducky_typer.cpp` | tool | blocked | NimBLE HID | `!LITE` | no |
| BLE Scan | `ble_common.cpp` | tool | rewrite | NimBLE scan → BlueZ | `!LITE` | no |
| BLE Hunter | `ble_hunter.cpp` | tool | rewrite | continuous ADV scan → BlueZ | `!LITE` | no |
| iBeacon | `ble_spam.cpp` | tool | blocked | NimBLE ADV mfg | `!LITE` | no |
| BLE Spam (Apple/Android/Windows/Samsung/Random) | `ble_spam.cpp` | tool | blocked | NimBLE / Bluedroid ADV spoof | always / `!LITE` | no |
| Spam Adv/Gap/TX/MAC settings | `ble_spam.cpp` | setting | blocked | spam ADV config | always | no |
| BLE Suite (Vulnerability Scan … Universal Attack Chain) | `BLE_Suite.cpp` | tool | blocked | NimBLE central/peripheral as used | `!LITE` | no |
| BLE Sniffer | `ble_sniffer.cpp` / Suite | tool | rewrite | BLE scan UI → BlueZ | `!LITE` / LITE | no |
| Ninebot | `ble_ninebot.cpp` | tool | rewrite | NimBLE GATT client → BlueZ | `!LITE` | no |
| BLE Name Flood | `name_flood_menu.cpp` | tool | blocked | NimBLE ADV name rotate | `EVIL_EXTENSIONS` | no |
| Wall Of Airtag | `wall_of_airtag_menu.cpp` | tool | rewrite | ADV scan Find My → BlueZ | `EVIL_EXTENSIONS` | no |
| FindMyEvil | `findmy_menu.cpp` | tool | blocked | NimBLE / GAP mfg advertise | `EVIL_EXTENSIONS` | no |
| Skimmer Detector | `skimmer_menu.cpp` | tool | rewrite | ADV scan heuristics → BlueZ | `EVIL_EXTENSIONS` | no |

---

## RF

| Label | Source | Role | Verdict | Deciding dependency | Gate | Lite SKU |
| --- | --- | --- | --- | --- | --- | --- |
| Scan/copy / Record RAW / Custom SubGhz / Protocol Library TX | `rf_*.cpp` | tool | os | Stock [Cap-CC1101-SubG-Chat](https://github.com/CardputerZero/Cap-CC1101-SubG-Chat) covers Sub-GHz Cap domain | always / `!LITE` | no |
| Protocol Library → Help | `rf_send.cpp` | tool | os | Cap-CC1101 docs/domain | `!LITE` | no |
| Spectrum / SquareWave Spec | `rf_spectrum.cpp` | tool | os | Cap-CC1101 domain | always / `!LITE` | no |
| RSSI Spectrum / Spectogram waterfall | `rf_spectrum.cpp` / `rf_waterfall.cpp` | tool | os | Cap-CC1101 domain | `!LITE` | no |
| Listen | `rf_listen.cpp` | tool | os | Cap-CC1101 domain | `!LITE` + audio | no |
| Bruteforce / Jammer | `rf_bruteforce.cpp` / `rf_jammer.cpp` | tool | os | Cap-CC1101 domain (not a Wave target) | `!LITE` | no |
| RF TX/RX Pin, Module, Frequency | `settings.cpp` via `rf_menu.cpp` | setting | os | Cap bring-up via stock Cap apps | always | no |

---

## NRF24

| Label | Source | Role | Verdict | Deciding dependency | Gate | Lite SKU |
| --- | --- | --- | --- | --- | --- | --- |
| Information | `nrf_common.cpp` | tool | rewrite | UI disclaimer only (NRF tools stay accessory) | always | no |
| Spectrum / MouseJack / NRF Jammer | `nrf_*.cpp` | tool | accessory | **nRF24L01+** SPI | always / `!LITE` | no |

---

## LoRa

| Label | Source | Role | Verdict | Deciding dependency | Gate | Lite SKU |
| --- | --- | --- | --- | --- | --- | --- |
| Chat / Antenna Check / username / frequency | `LoRaRF.cpp` | tool/setting | os | Stock [Cap-LoRa-1262-GPS](https://github.com/CardputerZero/Cap-LoRa-1262-GPS) / Cap-LoRa-1262 covers domain | `!LITE` | no |
| Meshtastic Chat / Nodes / Apply radio / mesh settings | `meshtastic/mesh_ui.cpp` | tool/setting | os | Cap LoRa app domain — do not re-port | `!LITE && HAS_LORA_CAP` | no |

---

## FM

| Label | Source | Role | Verdict | Deciding dependency | Gate | Lite SKU |
| --- | --- | --- | --- | --- | --- | --- |
| Brdcast std/rsvd/stop / FM Spectrum / Hijack TA | `fm/fm.cpp` | tool | accessory | **Si4713** I2C | `!LITE && FM_SI4713` | no |

---

## Infrared

| Label | Source | Role | Verdict | Deciding dependency | Gate | Lite SKU |
| --- | --- | --- | --- | --- | --- | --- |
| Univ Rem Learn / Use / Favorites / Browse / Delete | `infrared/kremote/*` | tool | rewrite | NEC Use/Learn/Browse/Read/Favorites in kvxputerzero over LIRC; stock IR-Remote still covers full remotes | always | no |
| Univ Rem Button Map / About / Orientation / Buttons | `kremote_menu.cpp` | tool/setting | os | IR-Remote UI prefs | always | no |
| IR HW Cardputer / Unit / Both | `kremote_menu.cpp` | setting | os | IR-Remote hardware select | always | no |
| IR folder Pick/Reset | `kremote_menu.cpp` | setting | os | IR-Remote library path | always | no |
| TV-B-Gone | `TV-B-Gone.cpp` | tool | onboard | IR TX blast tool — not stock IR-Remote | always | no |
| Custom IR (SD/LittleFS / Spam / Virtual) | `custom_ir.cpp` | tool | os | Covered by IR-Remote + `.ir` assets | always | no |
| IR Read | `ir_read.cpp` | tool | os | IR-Remote learn/read path | always | no |
| IR Jammer | `ir_jammer.cpp` | tool | onboard | IR TX carrier tool — unique vs IR-Remote | `!LITE` | no |
| TagTinker ESL actions | `tagtinker_menu.cpp` | tool | onboard | IR ESL tool — unique vs IR-Remote / IR-Chat | `EVIL_EXTENSIONS` | no |
| Ir TX/RX Pin / Repeats | `settings.cpp` | setting | os | IR-Remote / FactoryTest cover IR bring-up | always | no |

`.ir` libraries under `tools/sd_pack/` are **asset** (see Assets).

---

## Ethernet

| Label | Source | Role | Verdict | Deciding dependency | Gate | Lite SKU |
| --- | --- | --- | --- | --- | --- | --- |
| Scan Hosts | `ethernet_menu.cpp` | tool | onboard | W5500 SPI in firmware → replace with onboard Ethernet (SR9900A) | `!LITE` | no |
| DHCP Starvation | `DHCPStarvation.cpp` | tool | onboard | same Ethernet NIC path | `!LITE` | no |
| MAC Flooding | `MACFlooding.cpp` | tool | onboard | raw Ethernet frames on NIC | `!LITE` | no |

---

## USB

| Label | Source | Role | Verdict | Deciding dependency | Gate | Lite SKU |
| --- | --- | --- | --- | --- | --- | --- |
| PC Connect | `usb/pc_connect/pc_connect.cpp` | tool | drop | ESP USB CDC protocol only for this firmware | `!LITE` | no |
| kvxkeyboard HID (USB) | `hid_remote` via `usb_menu.cpp` | tool | blocked | ESP USB HID gadget | `!LITE && USB_as_HID` | no |
| BadUSB / USB Keyboard / USB Clicker | `ducky_typer.cpp` / `clicker.cpp` | tool | blocked | ESP USB HID gadget | `!LITE` | no |
| USB U2F | `u2f.cpp` | tool | blocked | ESP USB HID U2F | `!LITE && USB_as_HID` | no |
| BadUSB Hunter | `badusb_hunter.cpp` | tool | blocked | ESP USB Host/OTG | `!LITE && SOC_USB_OTG_SUPPORTED` | no |
| Mass Storage | `massStorage.cpp` | tool | blocked | ESP USB MSC gadget | `SOC_USB_OTG_SUPPORTED` | no |

Host-side scripts: **asset** under Assets (`PCConnect/`).

---

## GPS

| Label | Source | Role | Verdict | Deciding dependency | Gate | Lite SKU |
| --- | --- | --- | --- | --- | --- | --- |
| Scan WiFi / BLE / Both | `wardriving.cpp` | tool | accessory | GPS via Cap; WiFi/BLE scan side could be tools without Cap GPS | always | no |
| View Captures | `wd/wd_viewer.cpp` | tool | rewrite | FS CSV viewer (wardrive loot tool) | always | no |
| WD Config prefs | `wardriving.cpp` | setting | rewrite | prefs for wardrive tool | always | no |
| Wardriving Master | `wardriving_master_menu.cpp` | tool | blocked | ESP-NOW RX | `EVIL_EXTENSIONS` | no |
| Site Survey Help / Start | `site_survey.cpp` | tool | accessory | GPS UART + WiFi | `!LITE` | no |
| GPS Tracker | `gps_tracker.cpp` | tool | os | Cap-LoRa-1262-GPS covers GPS | `!LITE` | no |
| GPS Source / Baud / Pins / Configure LoRa Cap | `gps_menu.cpp` / `LoRaRF.cpp` | setting | os | Cap GPS bring-up | always / `HAS_LORA_CAP` | no |

---

## RFID

| Label | Source | Role | Verdict | Deciding dependency | Gate | Lite SKU |
| --- | --- | --- | --- | --- | --- | --- |
| Read / Scan / Load / Erase | `tag_o_matic.cpp` | tool | os | Stock [RFID](https://github.com/CardputerZero/RFID) | RFID HW enabled | no |
| Read 125kHz | `rfid125.cpp` | tool | os | Stock RFID app domain | `!LITE` | no |
| Module / Wiring help / Add MIF Key | `settings.cpp` / `rfid_shared.cpp` | setting/tool | os | Stock RFID | always | no |

---

## NFC

| Label | Source | Role | Verdict | Deciding dependency | Gate | Lite SKU |
| --- | --- | --- | --- | --- | --- | --- |
| Read / Scan / Load / Erase / Classic / Tags / NDEF / Lab / Parsers / EMV / SRIX | `nfc_*.cpp` / `emv_reader.cpp` / `srix_tool.cpp` | tool | os | Stock [Cap-CC1101-NFC](https://github.com/CardputerZero/Cap-CC1101-NFC) / [RFID](https://github.com/CardputerZero/RFID) | RFID HW / `!LITE` | no |
| Dump editor / Save as file / Help-like docs | various | tool | os | Stock NFC/RFID apps | always | no |
| Amiibolink / PN532 BLE / Chameleon BLE paths | `amiibo.cpp` / `pn532ble.cpp` / `chameleon.cpp` | tool | rewrite | BlueZ GATT to external companion (unique if not in stock RFID) | `!LITE` | no |
| PN532 UART / Killer Reader/Emulator/Sniffer | `PN532KillerTools.cpp` | tool | accessory | PN532Killer UART (not stock Cap) | `!LITE` | no |
| NFC Module / Wiring | `settings.cpp` / `rfid_shared.cpp` | setting | os | Stock Cap/RFID | always | no |

---

## Files

| Label | Source | Role | Verdict | Deciding dependency | Gate | Lite SKU |
| --- | --- | --- | --- | --- | --- | --- |
| SD Card | `files_menu.cpp` | tool | rewrite | Ported as Files browser (home + /media + /mnt); stock Files still available | always | no |
| LittleFS | `files_menu.cpp` | tool | rewrite | Covered by Files browser roots on Linux FS | always | no |
| my Network / AP mode / Stop / WebUi screen | `webInterface.cpp` | tool | rewrite | Cardputer-style remote WebUI tool (beyond Files) | always | no |
| File Share (same as WiFi) | `file_share.cpp` | tool | rewrite | HTTP share portal tool | always | no |
| Send/Recv File | FileSharing | tool | blocked | ESP-NOW / ESP serial peer | `!LITE` | no |
| Send/Recv Cmds | `serial_commands` | tool | blocked | ESP serial peer protocol | `!LITE` | no |
| Mass Storage | `massStorage.cpp` | tool | blocked | ESP USB MSC | `SOC_USB_OTG_SUPPORTED` | no |

---

## Scripts

| Label | Source | Role | Verdict | Deciding dependency | Gate | Lite SKU |
| --- | --- | --- | --- | --- | --- | --- |
| Dynamic `.js` / Load… | `scripts_menu.cpp` | tool | rewrite | BJS interpreter → reimplement or host scripts | `!LITE && !DISABLE_INTERPRETER` | no |

Sample scripts under `tools/sd_pack/menu/scripts/` are **asset**.

---

## Clock

| Label | Source | Role | Verdict | Deciding dependency | Gate | Lite SKU |
| --- | --- | --- | --- | --- | --- | --- |
| Clock face | `clock_menu.cpp` | tool | rewrite | Ported in kvxputerzero (`ClockFaceScreen`) | always | no |
| Timer | `clock_menu.cpp` | tool | rewrite | software countdown tool | always | no |

---

## Charge

| Label | Source | Role | Verdict | Deciding dependency | Gate | Lite SKU |
| --- | --- | --- | --- | --- | --- | --- |
| Charge screen | `charge_menu.cpp` / `charge_screen.cpp` | tool | rewrite | Ported in kvxputerzero Charge channel (`ClockFaceScreen` charge mode) | always | no |

---

## Tools (Others)

| Label | Source | Role | Verdict | Deciding dependency | Gate | Lite SKU |
| --- | --- | --- | --- | --- | --- | --- |
| Calculator | `calculator.cpp` | tool | rewrite | Ported in kvxputerzero (`CalculatorScreen` / `calc_lib`); stock Calculator optional | always | no |
| Calculators suite (Subnet … Flow and pipe) | `calc/calc_menu.cpp` | tool | rewrite | Engineering calc tools beyond stock Calculator | always | no |
| Media Player | `media_player.cpp` | tool | rewrite | Ported in kvxputerzero Tools → Media Player (`play_wav`); stock Music/Piano/Recorder remain | `HAS_NS4168_SPKR` | no |
| PDA Calendar / Add Event | `pda/pda_calendar.cpp` | tool | os | Stock [Calendar](https://github.com/CardputerZero/Calendar) | `!LITE` | no |
| PDA Notes, Memos, To-Do, Contacts, Alarms, World Clock | `pda/*.cpp` | tool | rewrite | PDA tools not covered by stock Calendar alone | `!LITE` | no |
| QRCodes (PIX / Custom / saved) | `qrcode_menu.cpp` | tool | rewrite | QR render to display | always | no |
| Barcode (Code 128 … UPC-A) | `barcode/barcode.cpp` | tool | rewrite | barcode → display | `HAS_KEYBOARD` | no |
| Passgen | `passgen/passgen.cpp` | tool | rewrite | local RNG passwords | `HAS_KEYBOARD` | no |
| TOTP Help / Show / Add / Delete | `totp/totp.cpp` | tool | rewrite | TOTP HMAC + FS | `HAS_KEYBOARD && !LITE` | no |
| Combo Crack / How to use / About | `combo/combo.cpp` | tool | rewrite | software UI | `HAS_KEYBOARD` | no |
| DTMF Dialer / Bluebox / Redbox / Misc | `dtmf/dtmf.cpp` | tool | onboard | tone gen → ALSA/speaker | keyboard + audio | no |
| DTMF Listen | `dtmf/dtmf.cpp` | tool | onboard | mic decode → MEMS mic / ALSA | + `MIC_*` | no |
| Morse Text / Key / Letters / Speed | `morse/morse.cpp` | tool/setting | onboard | speaker tones → ALSA | keyboard + audio | no |
| Morse Mic decode | `morse/morse.cpp` | tool | onboard | mic → ALSA capture | + mic | no |
| Codec / Hash (Base64, Hex, URL, MD5, SHA*) | `codec/codec.cpp` | tool | rewrite | software / mbedtls digests | `!LITE` | no |
| MQTT Connect / Sub / Pub / Log / topics | `mqtt/mqtt.cpp` | tool | rewrite | MQTT over STA | `!LITE` | no |
| Drone ID WiFi scan | `drone/drone.cpp` | tool | blocked | WiFi promiscuous OpenDroneID | `!LITE` | no |
| Drone ID BLE scan | `drone/drone.cpp` | tool | rewrite | BLE ADV scan → BlueZ | `!LITE` | no |
| Microphone Spectrum / Record | `mic.cpp` | tool | onboard | I2S mic → ALSA | `MIC_*` | no |
| iButton Read / Write / Save / Load / Pin | `ibutton.cpp` | tool/setting | accessory | OneWire GPIO | `!LITE` | no |
| LLM HTTP stream | `llm_chat_menu.cpp` | tool | rewrite | WiFi HTTP | `EVIL_EXTENSIONS` | no |
| UART LLM module | `llm_chat_menu.cpp` | tool | accessory | Grove UART LLM module | `EVIL_EXTENSIONS` | no |

---

## Modules

| Label | Source | Role | Verdict | Deciding dependency | Gate | Lite SKU |
| --- | --- | --- | --- | --- | --- | --- |
| Unit Scroll / Joystick probes & settings | `modules_menu.cpp` | tool/setting | accessory | Grove I2C Unit Scroll / Joystick2 | `UNIT_SCROLL` / `UNIT_JOYSTICK2` | no |
| PaHub reconnect / channels / scan | `modules_menu.cpp` | tool/setting | accessory | TCA9548A PaHub I2C | always | no |
| Bus Scan Help / I2C Scan / SPI pin map / PaHub scan | `bus_scan.cpp` | tool | onboard | I2C scan on Grove/EXT → Linux i2c | always | no |
| Companion `.bin` list | `modules_menu.cpp` | tool | drop | ESP companion firmware images | FS | no |

---

## Config

| Label | Source | Role | Verdict | Deciding dependency | Gate | Lite SKU |
| --- | --- | --- | --- | --- | --- | --- |
| Brightness / Dim Time / Orientation / UI Color / Accent / Theme | `config_menu.cpp` / `settings.cpp` | setting | rewrite | Colors, brightness, dim, sound, power in kvxputerzero Config; orientation still launcher | always | no |
| G0 Hold = Home | `config_menu.cpp` | setting | os | launcher key handling | always | no |
| Mainscreen Shortcuts | `app_catalog.cpp` | setting | os | launcher navigation | `HAS_KEYBOARD` | no |
| LED Color / Effect / Brightness / Blink | `led_control.cpp` | setting | accessory | Cardputer RGB LED (Zero path TBD) | `HAS_RGB_LED` | no |
| Sound / Volume | `config_menu.cpp` | setting | os | OS / Music / FactoryTest audio path | audio macros | no |
| InstaBoot / WiFi Startup / Startup App / Hide Apps | `config_menu.cpp` | setting | os | launcher / systemd / OS Wi-Fi | always | no |
| Clock NTP / Manual / DST / 12-24h | `settings.cpp` | setting | os | OS time / launcher clock | always | no |
| Keyboard Language | `mykeyboard.cpp` | setting | os | OS / Keyboard-Guide | always | no |
| LoRa Cap | `LoRaRF` | setting | os | Cap-LoRa stock app | `HAS_LORA_CAP` | no |
| Toggle BLE API | `settings.cpp` | setting | drop | hosted C6 BLE API for this firmware | `!LITE` | no |
| BadUSB layout / Key Delay / Show Output | `settings.cpp` | setting | blocked | BadUSB HID prefs | `!LITE` | no |
| BLE name | `config_menu.cpp` | setting | rewrite | only if shipping a BLE tool that needs a local name | always | no |
| WiFi AP Creds | `settings.cpp` | setting | os | OS SoftAP / network settings | always | no |
| Factory Reset | `config_menu.cpp` | setting | os | OS / FactoryTest | always | no |
| I2C Finder | `i2c_finder` | tool | onboard | I2C bus scan tool | always | no |
| Peripheral pin maps (CC1101, NRF24, PN532, LoRa, ST25, W5500, SD, I2C, UART, GPS) | `config_menu.cpp` | setting | os | Cap/stock apps + OS | always / `!LITE` | no |
| Deep Sleep / Sleep | `config_menu.cpp` | tool | drop | ESP sleep APIs | always | no |
| Restart / Power Off | `config_menu.cpp` | tool | os | OS power / systemd | always | no |
| Install App Store | `settings.cpp` | tool | os | Stock [Store](https://github.com/CardputerZero/Store) (ESP JS path also **drop**) | `!LITE` | no |
| About | `utils.cpp` | tool | os | launcher / FactoryTest device info | always | no |
| Dev Mode serial USB/UART / Disable DevMode | `config_menu.cpp` | setting | drop | ESP serial routing | `devMode` | no |

WAVES collapses Config into one **settings shell** candidate.

---

## Unreachable: Connect channel

[`src/menu/connect/connect_menu.cpp`](../src/menu/connect/connect_menu.cpp) is compiled on `MainMenu` but **not** listed in `kMenus`. The same Send/Recv File/Cmds leaves appear under **Files → Connect** (`!LITE`) and are audited there (blocked: ESP-NOW / serial peer).

---

## Assets (not code ports)

| Asset | Path | Note |
| --- | --- | --- |
| Themes | `tools/sd_pack/` → `/root/themes/` | reusable art; Zero UI is 320×170 |
| IR profiles | `support_files/infrared/` | Flipper-style `.ir` for IR ports |
| Scripts | `/menu/scripts/` | BJS samples; need interpreter rewrite |
| WiFi portals / wordlists / netops data | `support_files/wifi/`, `netops/`, `evil portals/` | data for rewrite SoftAP/HTTP tools |
| RF `.sub` library | `support_files/rf/library/` | needs CC1101 accessory |
| BLE / RFID / media samples | `support_files/ble/`, `rfid/`, `media/` | format-dependent |
| Companion `.bin` | `support_files/companions/` | ESP-only images — **drop** for Zero |
| PC Connect host | `PCConnect/` | Python host; device half is **drop**/blocked |

---

## Zero-only hardware (no kvxputer leaf)

| Capability | SKU | Note |
| --- | --- | --- |
| IMX219 camera | C154 | FactoryTest / Camera app — no kvxputer menu |
| BMI270 + BMM150 | C154 | FactoryTest / Compass — no kvxputer menu |
| HDMI 1080p30 | both | FactoryTest DRM precheck — no kvxputer menu |
| Onboard Ethernet SR9900A | both | Maps to Ethernet-channel *capability*; kvxputer code targets W5500 |

Do not design new apps for these in this audit.
