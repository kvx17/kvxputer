# Evil feature map (89 items)

Reference demo (local, gitignored): `resources/Evil-Cardputer-v1-5-4.ino` — not compiled. See [REFERENCE.md](REFERENCE.md).

Action: **reuse** = Bruce implementation; **port** = Evil-unique addon under `src/menu/<domain>/<addon>/`.

Gated by `-DEVIL_EXTENSIONS=1` (on for `m5stack-cardputer`, off for lite).

| # | Evil item | Category | Priority | Action | Addon |
|---|-----------|----------|----------|--------|-------|
| 0–5 | Scan/Select/Clone/SSID/Pass/MAC | WiFi | P0 | reuse | Connect + WiFi Config |
| 6–11 | Captive portal / creds / monitor | WiFi | P0 | reuse | `evil_portal.cpp` |
| 12–19 | Probe / Karma* | WiFi | P1 | reuse Karma + port probe CRUD/spear | `wifi/probe/` |
| 20 | Wardriving | GPS | P1 | reuse | `gps/wardriving.cpp` |
| 21 | Wardriving Master | GPS | P1 | port | `gps/wardriving_master/` |
| 22–25 | Beacon/Deauth/Twin | WiFi | P0 | reuse | `wifi_atks.cpp`, `deauther.cpp` |
| 26, 32 | Handshake Master / Check | WiFi | P1 | port | `wifi/handshake_master/` |
| 27–31 | Raw / client sniff / visualizer | WiFi | P0 | reuse | `sniffer.cpp`, `channel_analyzer.cpp` |
| 33 | Wall Of Flipper | WiFi | P1 | port | `wifi/wall_of_flipper/` |
| 34 | Connect to network | WiFi | P0 | reuse | |
| 35–39 | SSH / scans | NetOps | P0 | reuse | first-class NetOps rows |
| 40 | Web Crawler | NetOps | P1 | port | `netops/web_crawler/` |
| 41 | PwnGrid Spam | WiFi | P1 | reuse | `pwnagotchi/` (`kvxgotchi_start`) |
| 42 | Skimmer Detector | BLE | P2 | deferred | (BLE if ported later) |
| 43 | Mouse Jiggler | Others | P1 | reuse | HID Remote |
| 44 | BadUSB | Others | P0 | reuse | `ducky_typer` |
| 45 | Bluetooth Keyboard | BLE | P0 | reuse | HID Remote |
| 46 | Reverse TCP Tunnel | NetOps | P1 | port | `netops/reverse_tcp/` |
| 47 | DHCP Starvation (WiFi) | NetOps | P1 | port | `netops/dhcp_starvation/` |
| 48–49 | Rogue DHCP STA/AP | NetOps | P1 | port | `netops/rogue_dhcp/` |
| 50 | Switch DNS | NetOps | P1 | port | `netops/switch_dns/` |
| 51 | Network Hijacking | NetOps | P1 | port | `netops/hijack/` |
| 52–54 | Printer | NetOps | P2 | port | `netops/printer/` |
| 55 | HoneyPot | NetOps | P2 | port | `netops/honeypot/` |
| 56 | LLM Chat Stream | Others | P2 | port | `others/llm_chat/` (`HAS_LLM_MODULE`) |
| 57 | EvilChatMesh | NetOps | P2 | port | `netops/chat_mesh/` |
| 58 | SD on USB | Files | P0 | reuse | Files → Mass Storage |
| 59 | Responder | NetOps | P0 | reuse | `responder.cpp` |
| 60 | WPAD Abuse | NetOps | P1 | port | `netops/wpad/` |
| 61–62 | Crack / clean NTLMv2 | NetOps | P1 | port | `netops/ntlm/` |
| 63 | FileManager | Files | P0 | reuse | `files_menu.cpp` |
| 64 | UART Shell | NetOps | P1 | port | `netops/uart_shell/` |
| 65–69 | SIP Toolkit | NetOps | P2 | port | `netops/sip/` |
| 70 | CCTV Toolkit | NetOps | P2 | port | `netops/cctv/` |
| 71 | SSDP Poisoner | NetOps | P2 | port | `netops/ssdp/` |
| 72 | SkyJack | NetOps | P2 | port | `netops/skyjack/` |
| 73 | WiFi Dead Drop | WiFi | P2 | port | `wifi/dead_drop/` |
| 74 | BLENameFlood | BLE | P1 | port | `ble/name_flood/` |
| 75 | Wall Of Airtag | BLE | P1 | port | `ble/wall_of_airtag/` |
| 76 | FindMyEvil | BLE | P1 | port | `ble/findmy/` |
| 77–78 | UPnP | NetOps | P2 | port | `netops/upnp/` |
| 79 | LDAPDump | NetOps | P2 | port | `netops/ldap/` |
| 80 | IMSI Catcher (EAP Identity) | NetOps | P2 | port | `netops/imsi_eap/` |
| 81 | Open Wifi Checker | WiFi | P2 | port | `wifi/open_wifi/` |
| 82 | ESP32C5 Serial | WiFi | P2 | port | `wifi/c5_serial/` |
| 83 | Aircrack | WiFi | P2 | port | `wifi/aircrack/` |
| 84 | Autodiscover Abuse | NetOps | P2 | port | `netops/autodiscover/` |
| 85 | CIW Zeroclick | NetOps | P2 | port | `netops/ciw/` |
| 86 | TagTinker ESL | Infrared | P2 | port | `infrared/tagtinker/` |
| 87 | CSI Radar | WiFi | P2 | port | `wifi/csi_radar/` |
| 88 | Settings | Config | P0 | reuse | `config_menu.cpp` |

Slave firmware and host utilities stay in `resources/Evil-M5Project-main/` (not compiled into Cardputer firmware).
