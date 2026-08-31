# Evil feature map (89 items)

Reference demo (local, gitignored): `resources/Evil-Cardputer-v1-5-4.ino` — not compiled. See [REFERENCE.md](REFERENCE.md).  
Action: **reuse** = Bruce implementation; **port** = Evil-unique; **stub** = menu wired, logic pending.

| # | Evil item | Category | Priority | Action |
|---|-----------|----------|----------|--------|
| 0–5 | Scan/Select/Clone/SSID/Pass/MAC | WiFi | P0 | reuse Bruce Connect + Config |
| 6–11 | Captive portal / creds / monitor | WiFi | P0 | reuse Bruce Evil Portal |
| 12–19 | Probe / Karma* | WiFi | P1 | reuse Karma + stub probe CRUD/spear |
| 20–21 | Wardriving / Master | WiFi | P1 | stub Master |
| 22–25 | Beacon/Deauth/Twin | WiFi | P0 | reuse Wifi Atks |
| 26–33 | Handshake/sniff/WoF | WiFi | P1 | stub extras under Evil WiFi Extras |
| 34 | Connect to network | WiFi | P0 | reuse |
| 35–40 | SSH / scans / crawler | NetOps | P0–P1 | reuse SSH/hosts; stub crawler |
| 41 | PwnGrid Spam | WiFi | P1 | reuse Brucegotchi |
| 42 | Skimmer Detector | NetOps | P2 | deferred |
| 43 | Mouse Jiggler | Others | P1 | reuse USB Clicker |
| 44 | BadUSB | Others | P0 | reuse |
| 45 | Bluetooth Keyboard | BLE | P0 | reuse BLE Keyboard |
| 46–51 | Reverse TCP / DHCP / DNS / hijack | NetOps | P1 | stub |
| 52–54 | Printer | NetOps | P2 | stub |
| 55–58 | Honey/LLM/Mesh/SD USB | NetOps/Others | P2 | stub |
| 59–62 | Responder/WPAD/NTLM | NetOps | P1 | reuse Responder; stub WPAD/NTLM |
| 63 | FileManager | Files | P0 | reuse |
| 64 | UART Shell | NetOps | P1 | stub |
| 65–72 | SIP/CCTV/SSDP/SkyJack | NetOps | P2 | stub |
| 73 | WiFi Dead Drop | WiFi | P2 | stub |
| 74–76 | BLE flood / AirTag / FindMy | BLE | P1 | stub evil_ble |
| 77–87 | UPnP/LDAP/IMSI/C5/Aircrack/… | NetOps/WiFi | P2 | stub |
| 88 | Settings | Config | P0 | reuse + Unit Scroll menu |
