# kvxputerzero

**kvxputerzero** is the kvxputer toolkit as a Cardputer Zero app. It is one arm64 package: a Wii-style **3×2 channel grid** (purple tiles, green selection) and the same kinds of tools as the Cardputer firmware — Wi‑Fi, NetOps, BLE, Sub-GHz, NRF24, LoRa, FM, Infrared, Ethernet, GPS, RFID, NFC, Files, Scripts, and everyday utilities — running on the Zero’s Linux image instead of an ESP32.

On launch it plays a matrix rain with the **KVXPUTER** wordmark for 5 seconds. Press any key to skip. The same animation is under **Tools → Matrix**.

**Use responsibly:** only on networks and devices you own or have explicit permission to test.

Keyboard layout, the desktop emulator, and the cross-compile steps are also in [`app/README.md`](app/README.md).

---

## Highlights

- **Channel menu** — 3×2 grid, several pages. The footer shows the selected channel and the page (`1/4`).
- **Discovery** — the same discovery tools as Cardputer ADV (BLE scan, Wall of Flipper, Wall of AirTag, spectrum, wardrive, and the rest that this port ships).
- **Wireless toolkit** — Wi‑Fi analyzer, BLE scan and walls, Cap radios (CC1101, NRF24, SX1262, Si4713), Infrared, RFID, and a full NFC channel.
- **Everyday tools** — scientific calculator, engineering calculators, codec, passwords, TOTP, PDA, media, clock, charge, camera, gallery.
- **PC Connect scripts** — browse and export the Python host that talks to a Cardputer running kvxputer.
- **SSH CLI** — `kvxputerzero-cli` runs the same tools from a terminal when the screen app is closed.

---

## Using the app

Open **kvxputerzero** from APPLaunch, or run `kvxputerzero` in a shell on the device. The graphical app needs the framebuffer; over SSH use the [CLI](#ssh-cli) instead.

### Channel home

The bottom bar is the Cardputer Zero **4 5 6 7 8** row.

| Key | On the channel home | Inside a menu |
| --- | --- | --- |
| **4 / 8** | Previous / next page | **4** back, **8** home |
| **5 / 7** | Up / down | Up / down |
| **6** | Open the channel | Select |
| **Fn + F / X / Z / C** | Up / down / left / right | Same. Menus also accept those keys without Fn |
| **Enter** | Open | Activate |
| **Space** | Search menus and tools | Activate |
| **Esc** | — | Back one screen |
| **Hold Esc** for 3 seconds | Quit the app. The bar appears after 1 second | Quit the app. The bar appears after 1 second |
| **Hold Tab** for 3 seconds | Lock the screen. The bar appears after 1 second | Lock the screen. The bar appears after 1 second |
| **Fn+H** | Keyboard help | Keyboard help |
| **Fn+J** | Screenshot | Screenshot |

**Shift** is uppercase. **Sym** is the blue symbol on a key (**Sym+B** is `.`, **Sym+V** is `,`, **Sym+N** is `/`). **Fn** is the orange shortcut. Number keys stay digits so **4–8** keep driving the bar.

Settings, PDA data, scans, and radio prefs live under `~/.config/kvxputerzero/`.

---

## Channels and tools

Cap radios (RF, NRF24, LoRa, FM, NFC) need the matching Cardputer Cap fitted and the user in the `spi`, `i2c`, and `gpio` groups. Do not hot-plug a Cap. Optional bus overrides, one path per file:

| File under `~/.config/kvxputerzero/` | Bus |
| --- | --- |
| `spi_cc1101.txt` | CC1101 |
| `spi_nrf24.txt` | NRF24 |
| `spi_sx1262.txt` | SX1262 |
| `i2c_si4713.txt` | Si4713 |
| `i2c_pn532.txt` | PN532 |

Cap radios default to `/dev/spidev0.1` (CE1). Cap NFC (ST25R3916) shares that SPI, with soft-CS on GPIO22 and IRQ on GPIO23.

### WiFi

Onboard scan goes through `nmcli`. A USB adapter in monitor mode is optional and only used when you pick it.

| Tool | Purpose |
| --- | --- |
| **WiFi Analyzer** | Channel bars and SSID list. **F/X** pick a channel, **Z/C** change dwell, **Enter** lists APs on that channel, **L** lists all APs, **K** locks or hops (monitor mode), **E** exports CSV, **Esc** back. |
| **Monitor adapter** | Choose onboard Wi‑Fi or a USB stick that can do monitor mode. Saved in `wifi_monitor_iface`. |
| **Connect / Disconnect / AP info** | Join or leave a network and show the current AP. |
| **Net Toolkit** | Port scan, banners, ping, and small lookups while associated. |
| **Scan Hosts** | Hosts on the local network. |
| **Listen TCP / Client TCP** | Raw TCP listen or outbound client. |
| **SOCKS4 Proxy** | SOCKS4 proxy. |
| **TelNET** | Telnet client. |
| **File Share** | Browser file access to the device over Wi‑Fi. |
| **WiFi Dead Drop** | Leave or pick up a file on the LAN. |
| **LAN Watch** | Watch the gateway and DNS while associated. |
| **PineAP Hunter** | Look for rogue multi-SSID access points. |
| **Open networks** | Open and weak networks from the last scan. |
| **AP detail** | Details for one access point. |
| **Roku Remote** | Device list, then a D-pad remote. Also under Infrared. |
| **WireGuard** | Tunnel helper. |
| **Printer Tools** | Network printer discovery. |
| **UPnP list** | UPnP devices on the LAN. |
| **HoneyPot** | Lightweight listeners. |
| **SSH (launcher)** | Opens the system SSH client. |
| **Wall Of Flipper** | Flipper Zero devices seen over BLE. |
| **Check Handshakes** | Inspect captured handshake files. |

USB monitor capture uses `/usr/lib/kvxputerzero/kvxputerzero-wifimon`. The `.deb` sets that helper up on install.

**Roku Remote keys:** **F/X/Z/C** are the D-pad, **Enter** is Select, **Backspace** is Back. **4** Vol−, **5** Rew, **6** Play, **7** FF, **8** Vol+. Tap **Esc** to switch Remote and Keyboard; hold **Esc** to leave. **S** search, **Q** Home, **M** mute, **I** info, **G** replay, **A** apps, **B** audio status, **P** power, **R** find remote, **D** devices, **H** help.

### Discovery

Discovery tools, matching the Cardputer ADV catalog: BLE Scan, BLE Hunter, Wall Of Flipper, Wall Of AirTag, Skimmer Detector, WiFi Analyzer, PineAP Hunter, LAN Watch, Scan Hosts, Open networks, Roku Remote, RF Spectrum, NRF Spectrum, WD Both, Site Survey, Wardriving Master, and UPnP Tools. **Enter** opens the one you highlight. Search from the channel home (Space) finds these and every other menu item.

### NetOps

LAN tools that do not need a radio menu of their own. Several entries are the same code as under WiFi.

| Tool | Purpose |
| --- | --- |
| **Net Toolkit / Scan Hosts / Listen TCP / Client TCP / SOCKS4 / TelNET** | Same as under WiFi. |
| **File Share / Printer Tools / UPnP list / HoneyPot / SSH (launcher)** | Same as under WiFi. |
| **UART Shell** | Serial shell on a UART you pick. |
| **Loot Console** | Browse captured portal, responder, and hash files. |
| **Check Handshakes** | Handshake files. |
| **NTLM Clean** | Clean up NTLM hash captures. |
| **SMB Status** | Read-only SMB/NetBIOS probe. |
| **SIP OPTIONS** | SIP OPTIONS probe. |
| **LDAP Read** | LDAP read against a server you name. |

### BLE

Scan and identify nearby advertisers. Raw advertisements come from `/usr/lib/kvxputerzero/kvxputerzero-blescan` (set up by the package install).

| Tool | Purpose |
| --- | --- |
| **BLE Scan** | Nearby advertisers. |
| **BLE Hunter** | Follow one device. |
| **BLE Sniffer** | Advertisement detail view. |
| **Wall Of Flipper** | Flipper Zero BLE advertisements. |
| **Wall Of Airtag** | AirTag / Find My advertisements. |
| **Skimmer Detector** | Heuristics for BLE skimmer modules. |
| **Drone ID (BLE)** | OpenDroneID-style advertisements. |
| **Disconnect** | Drop the current BLE session. |
| **Ninebot / Amiibolink / PN532-BLE / Chameleon Status** | Companion status: connect and read. They do not clone those devices. |

### RF (Cap CC1101)

Sub-GHz tools for the CC1101 Cap. There is no jammer and no bruteforce.

| Tool | Purpose |
| --- | --- |
| **Cap Status** | Is the CC1101 answering on SPI. |
| **Spectrum** | Live RSSI across the band. |
| **Listen** | Demodulated listen. |
| **Record RAW** | Capture a remote into a `.sub` file. |
| **Replay / Custom** | Send a capture or a custom frame. |
| **Protocol Library** | Browse and send known `.sub` presets. |
| **Send Test** | Short test transmission. Confirm before it transmits. |

### NRF24

NRF24L01 on the Cap SPI bus. There is no MouseJack and no jammer.

| Tool | Purpose |
| --- | --- |
| **Information** | What the module reports. |
| **Module Status** | SPI presence. |
| **Spectrum** | 2.4 GHz channel view. |
| **Listen** | Receive payloads. |
| **Send Payload** | Transmit one payload. |

### LoRa (Cap SX1262)

| Tool | Purpose |
| --- | --- |
| **Cap Status** | SX1262 presence and antenna switch. |
| **Chat** | Text chat on the configured frequency. |
| **Antenna Check** | RSSI with the antenna path on and off. |
| **Frequency / Username** | Radio frequency and the name other nodes see. |
| **Meshtastic** | Nodes and chat over a Meshtastic USB serial port (Phone API, or line mode when `lora/mesh_mode.txt` is `text`). |

Meshtastic prefs live in `~/.config/kvxputerzero/lora/`: `mesh_port.txt` (for example `/dev/ttyACM0`), optional `mesh_baud.txt`, and `mesh_mode.txt`. The Cap GPS UART (`/dev/serial0`) is not the mesh port.

### FM (Si4713)

| Tool | Purpose |
| --- | --- |
| **Information / Module Status** | I2C presence of the Si4713. |
| **Broadcast** | Transmit on a chosen FM frequency. |
| **Stop** | End the transmission. |
| **Spectrum** | RSSI across the FM band. |

### Infrared

Onboard IR is TX GPIO12 and RX GPIO13. The library is Flipper-style `.ir` files.

| Tool | Purpose |
| --- | --- |
| **Use** | Send a saved remote. |
| **Learn** | Capture a button into the library. |
| **Browse** | Walk the `.ir` library. |
| **Read** | Show an unknown code as it arrives. |
| **Favorites** | Pinned remotes. |
| **Config** | Library folder and a TX/RX pin test. |
| **TV-B-Gone** | Common TV power codes. |
| **TagTinker ESL** | Electronic shelf-label helpers. |
| **IR Carrier** | Raw carrier output. |
| **Roku Remote** | Same remote as under WiFi. |

### Ethernet

| Tool | Purpose |
| --- | --- |
| **Link status** | Onboard Ethernet link, address, and gateway. |
| **Scan Hosts** | Hosts on the Ethernet LAN. |

### GPS

Wardriving needs a GPS: the LoRa Cap’s GPS or a Unit GPS v1.1 on Grove. Both use `/dev/serial0` at 115200. Unit GPS turns on Grove UART and 5V and prefers raw NMEA over gpsd. Cap GPS turns on `ext_5v_out`. If Unit GPS shares the Grove port with Unit Scroll, disable `scrolld`’s Grove setup. The desktop emulator has no GPS.

| Tool | Purpose |
| --- | --- |
| **Wardriving** | **Scan WiFi**, **Scan BLE**, or **Scan Both** into a live table and a WiGLE CSV. **View Captures** opens saved sessions. **WD Config** holds scan, quality, and upload options. **Wardriving Master** is a stub. |
| **Site Survey** | Channel airtime with a GPS fix. |
| **GPS Tracker** | A track written as GPX. |
| **Config** | Hardware (`cap` or `unit_v11` in `gps/hardware.txt`), source, NMEA path, baud, live status, captures folder. |

### RFID

A thin UID channel on the Cap ST25 or an optional PN532. Full card tools are under **NFC**.

| Tool | Purpose |
| --- | --- |
| **Module Status** | Which reader answered. |
| **Read / Scan** | UID and card type. |
| **Load / Save** | Dump files for that UID. |

### NFC

Same shape as the Cardputer ADV NFC menu. The reader is the Cap ST25R3916 (on the Cap coil, not the RP-SMA) or an optional PN532. Dumps go to `~/.config/kvxputerzero/nfc/` as `.nfc` and `.rfid`.

The top menu stays short: **Module Status**, **Read Tag**, **Emulate Tag**, **Write Tag**, **Scan tags**, **Load file**, **Erase data**, then the submenus.

| Submenu | What is inside |
| --- | --- |
| **Classic...** | Dict attack, NFC Magic, dump editor. Detect Reader and MFKey are stubs. |
| **Tags...** | ISO15693, DESFire browser, FeliCa services, forced-type read. Ultralight-C is a stub. |
| **NDEF tools...** | Write, emulate, read records, save as file, memory dump, chip info, lock, password, format, one NFC command. |
| **Lab...** | APDU script, compare a file to a live tag, field poll. |
| **Parsers...** | Amiibo, transit parse, ePassport (your own documents), Picopass / iCLASS, NFC e-ink. |
| **Companions...** | Read EMV (PN532), SRIX, Amiibolink, PN532 BLE, PN532 UART, Chameleon. |
| **Config** | Module (Auto / ST25 / PN532), wiring help, add a MIFARE key. |

### Files

| Tool | Purpose |
| --- | --- |
| **Browse** | Home and mounts. Text preview. Delete asks first. |
| **File Share (Wi-Fi)** | Opens the Wi‑Fi File Share tool. |

### Camera

Live preview. On the desktop build the screen is labeled Simulation. On the device it uses V4L2 or CSI when a camera is present.

**H/J** or left/right change the filter. Up/down change brightness. **Enter** or **6** takes a still (`~/Pictures/DCIM/Camera`). **R** records MJPEG AVI (`~/Videos/Camera`). **G** cycles framing guides. **1** hides the chrome.

### Gallery

Stills under `~/Pictures` (JPEG and PNG), including subfolders. **Enter** opens one photo with zoom and pan. **1** hides the chrome. **8** resets the zoom. **Esc** goes back.

### Scripts

| Tool | Purpose |
| --- | --- |
| **Local scripts** | Run scripts from the allowlist in `~/.config/kvxputerzero/scripts/`. |
| **PC Connect scripts** | Browse the bundled Python host. |
| **PC Connect how to** | Short instructions for the host script. |

### PC Connect

This channel does not turn the Zero into a USB radio dongle. It carries the Python scripts that a **PC** uses to drive a Cardputer running the kvxputer **PC Connect** app.

| Tool | Purpose |
| --- | --- |
| **How to use** | Plug in the Cardputer, open PC Connect on it, run `pcconnect.py` on the PC. |
| **What you need** | Python 3, `pyserial`, a data cable. Linux or macOS. |
| **Browse scripts** | Read the bundled `pcconnect.py`, `protocol.py`, and `apps/`. |
| **Export to config** | Copy that tree to `~/.config/kvxputerzero/pcconnect/` so you can move it to a PC. |

The bundled copy is [`app/assets/pcconnect/`](app/assets/pcconnect/README.md).

### Clock / Charge

| Channel | Purpose |
| --- | --- |
| **Clock** | Full-screen clock. **Enter** opens the Timer. |
| **Charge** | Charge-style face, color cycle, backlight steps. |

### Tools

| Tool | Purpose |
| --- | --- |
| **Calculator** | Scientific: `sin(90)`, `2^10`, `ans*2`. **Enter** evaluates. Empty field + **D** toggles DEG/RAD. |
| **Calculators** | Subnet, base converter, byte units, resistor color, LED resistor, voltage divider, baud bit-time, dBm, epoch, temp/length/AWG, bitmask, CRC, units, Ohm’s law, voltage drop, motor, gas law, flow and pipe, parallel resistors, RC timer, battery life, frequency, PWM, percentage, date and time difference, tip split, distance. |
| **Codec / Hash** | Base64, hex, URL encode/decode, MD5, SHA1, SHA256. |
| **Passgen** | **Z/C** length, **F/X** count, **Enter** generates. **Sym+U/L/D/C** toggle charsets. **Sym+S** appends to `passwords.txt`. |
| **TOTP** | Authenticator codes in `totp.json` (mode 0600). Needs the right clock. **Z/C** switch accounts. |
| **Combo** | Combinations from a charset and a length. |
| **QRCodes / Barcode** | **Enter** draws the code. **Space** inverts a QR. Type **B** for `.` in a URL or IP. |
| **PDA** | 4×2 hub: Notes, Memos, To-Do, Calendar, Contacts, Alarms, World Clock, Calculator. Data is under `pda/`. **1–8** open a tile, **9** rebinds. Lists: **Del** deletes, **8** or **Sym+N** creates. Calendar: **Z/C** day, **F/X** week, **L/M** month. Alarms only fire while the app is open. |
| **Media Player** | Play audio files through the system player. |
| **Timer** | Countdown. Also opened from Clock. |
| **Matrix** | The boot rain. Any key leaves early. |
| **DTMF / Morse** | Tones and Morse through the speaker. |
| **Microphone** | Level and a short recording. |
| **MQTT** | Connect, subscribe, publish. |
| **LLM Chat** | Chat against an HTTP endpoint you configure. |
| **Drone ID** | Same BLE drone advertisements as under BLE. |
| **iButton** | 1-Wire iButton read. The live read is also under Modules. |

On calculator and codec screens, type in the field and press **Enter**. **Esc** returns to that tool’s menu, then to Tools. After **Enter** on a calculator, **F/X** scroll the result.

### Modules

Grove and Cap bring-up. PORT-style modules share the Grove connector; do not put two of them on it at once.

| Tool | Purpose |
| --- | --- |
| **Cap Probe** | Which Cap chips answer (power, SPI, I2C). |
| **Bus Scanner** | I2C addresses on the Grove bus. |
| **Unit Scroll** | Encoder at I2C `0x40`. |
| **Unit Joystick** | Stick at I2C `0x63`. |
| **UART Shell** | Same serial shell as NetOps. |
| **PaHub Scan** | Channels on a PaHub mux (`0x70`–`0x77`). |
| **PaHub presence** | Whether the mux itself answers. |
| **iButton Read** | Read a 1-Wire iButton. |
| **PN532Killer UART** | Status of a UART-attached PN532Killer. |
| **UART LLM** | Chat over a UART link instead of Wi‑Fi. |

### Config

| Entry | Purpose |
| --- | --- |
| **Main color / Accent color / Background** | Theme. Swatches start from the kvxputer purple, green, and black. |
| **Reset colors** | Restore the defaults and return home. |
| **Brightness** | Panel backlight. |
| **Dim time** | Idle time before the panel dims. |
| **Sound** | UI beeps. |
| **Power** | Reboot or power off. |
| **About** | Version and device. |

### USB

USB HID, BadUSB, and mass storage are not available in this app. The tile explains that and returns you home. File copy uses the Zero’s own Linux tools (SSH, `scp`).

---

## SSH CLI

Quit the graphical app first (hold **Esc** on the channel home). Then:

```bash
kvxputerzero-cli                 # channel, then tool
kvxputerzero-cli list            # channels
kvxputerzero-cli list wifi       # tools in one channel
kvxputerzero-cli run tools codec b64e hello
kvxputerzero-cli run --yes rf rf_send_test   # confirm a transmit
```

The CLI uses the same `~/.config/kvxputerzero/` tree as the screen app. Arrows or **j/k** move, **Enter** opens, **Esc** or **q** goes back, **Ctrl-C** cancels a live tool. Do not run radio, SPI, or Bluetooth tools while the graphical app is still open.

---

## Run on a PC (desktop emulator)

Useful for the UI. Radios, GPS, and the camera are simulated or report that the hardware is missing. You need a graphical session (`DISPLAY` set), CMake ≥ 3.31, Ninja, SDL2, libpng, and FreeType.

```bash
cd kvxputerzero/app
cmake --preset linux-x86-64
cmake --build --preset linux-x86-64-dbg
./build/linux-x86-64/Debug/kvxputerzero
```

Release build: `cmake --build --preset linux-x86-64-rel`, then `./build/linux-x86-64/Release/kvxputerzero`.

macOS presets are `darwin-arm64` and `darwin-x86-64`.

---

## Install on Cardputer Zero

Build the `.deb` on a PC. Do not compile on the device (512 MB RAM).

Needs `aarch64-linux-gnu-g++` (Arch: package `aarch64-linux-gnu-gcc`; Debian/Ubuntu: `g++-aarch64-linux-gnu`), plus the same CMake and Ninja as the desktop build.

```bash
cd kvxputerzero/app
cmake --workflow --preset cp0-cross-package
```

The package is `dist/kvxputerzero_<version>_revN_arm64.deb`.

Copy and install (replace the user and address):

```bash
scp dist/kvxputerzero_*_arm64.deb kvx@192.168.50.153:~/
ssh kvx@192.168.50.153 'sudo apt install -y ~/kvxputerzero_*_arm64.deb'
```

Or from `kvxputerzero/app`:

```bash
REMOTE_USER=kvx REMOTE_HOST=192.168.50.153 ./deploy.sh
```

`deploy.sh` only copies the package. It does not publish to the App Store.

Launch from APPLaunch, or run `kvxputerzero`.

**Update** — rebuild, copy the new `.deb`, then `sudo apt install -y ~/kvxputerzero_*_arm64.deb`. A newer package replaces the installed one.

**Remove:**

```bash
sudo apt remove kvxputerzero
```

**Also delete settings:**

```bash
sudo apt purge kvxputerzero
rm -rf ~/.config/kvxputerzero
```

Publishing to the Cardputer Zero Store is separate. Store metadata is [`app/app-builder.json`](app/app-builder.json).

---

## Where things live

| Path | What it is |
| --- | --- |
| `app/src/model/channel_registry.h` | Channel ids and labels |
| `app/src/model/tool_registry.h` | Tool ids shared by the GUI and the CLI |
| `app/src/app/tool_screens.cpp` | Channel menus and tool screens |
| `app/src/cli/` | `kvxputerzero-cli` |
| `app/assets/pcconnect/` | Bundled PC Connect host scripts |
| `~/.config/kvxputerzero/` | Theme, PDA, TOTP, GPS, NFC dumps, LoRa, scripts |

---

## License

The app package metadata is MIT ([`app/app-builder.json`](app/app-builder.json)). The kvxputer repository that contains this tree is AGPL-3.0-or-later; see [LICENSE](../LICENSE).

Maintained at [github.com/kvx17/kvxputer](https://github.com/kvx17/kvxputer).
