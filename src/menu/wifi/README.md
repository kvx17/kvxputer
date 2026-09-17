# WiFi menu domain

Feature modules in this folder:

| File | Feature |
|------|---------|
| `wifi_menu.cpp` | Main menu router |
| `evil_portal.cpp` | Captive portal |
| `sniffer.cpp` | PCAP / handshake capture |
| `wifi_atks.cpp` | Deauth, beacon, karma |
| `netcut.cpp` | ARP spoof / netcut |
| `responder.cpp` | LLMNR/NBT-NS responder |
| `probe/` | Probe attack / sniff / spear + CRUD |
| `handshake_master/` | Handshake Master + Check Handshakes |
| `wall_of_flipper/` | Flipper BLE beacon wall |
| `dead_drop/` | WiFi file drop AP |
| `open_wifi/` | Open AP dashboard |
| `aircrack/` | On-device wordlist vs captured handshake |
| `csi_radar/` | Channel RSSI heatmap (promiscuous RX) |
| `c5_serial/` | UART toolkit for ESP32-C5 slave |

Support files: `/support_files/wifi/` (captures, portals, probes, wordlists, deaddrop, wof).
