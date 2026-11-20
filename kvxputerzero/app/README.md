# kvxputerzero — user guide

**kvxputerzero** is a Cardputer Zero app: a Wii-style channel home (like Cardputer kvxputer) with Tools, Config, and placeholders for later channels.

On launch it plays a live matrix rain with the **KVXPUTER** wordmark for 5 seconds. **Press any key to skip.** The same animation is in **Tools → Matrix**.

This guide covers **how to use the app**, **how to run the desktop emulator**, and **how to build and install it on a Cardputer Zero**.

---

## How to use

### Channel home

On launch you get a **3×2** grid of channels (purple tiles, green selection border). The footer shows the selected channel name and page (`1/4`, etc.).

The bottom bar is the Cardputer Zero **4 5 6 7 8** row. The green number is the key; the icon is what it does on this screen.

| Control | Action |
| --- | --- |
| **Fn + F / X / Z / C** | Up / down / left / right. Menus also accept those keys bare |
| **Letter key** | Types the letter, including **F X Z C** while a text field is open |
| **Sym + letter** | Blue punctuation on that key (**Sym+Q**=`~` **Sym+B**=`.` **Sym+V**=`,` **Sym+N**=`/` …). On device many Sym chords arrive as ready-made codes (`KEY_DOT`, `F13`…). Desktop: click **sym** or hold **Left Alt** |
| **Fn + letter** | Orange Fn legend (Help, Insert, media, arrows) |
| **4 / 8** on the channel home | Previous / next page |
| **5 / 7 / 6** on the channel home | Up / down / open |
| **4 / 5 / 6 / 7 / 8** on a menu | Back / up / select / down / home |
| **Enter** or **Space** | Open or activate |
| **Esc** | Go back one screen |
| **Hold Esc** (on home) | Quit the app |
| **Fn+H** | Keyboard help overlay |
| **Fn+J** | Screenshot |

Letters are bare keys. **Shift** is uppercase, **Sym** is the blue symbol, **Fn** is the orange shortcut. Number keys stay digits so **4–8** keep driving the bar; **Fn+1**…**0** are F1–F10; **Sym+1**… emit `!@#…`.

### What’s available now

| Channel | What you get |
| --- | --- |
| **Tools** | **Calculator** (scientific), Calculators (engineering), Codec / Hash, Passgen, TOTP, Combo, QR / Barcode, **PDA**, Media Player, Timer, **Matrix**, DTMF, Morse, Microphone, MQTT, LLM Chat, Drone ID (BLE), iButton hint |
| **WiFi / NetOps** | Connect, Disconnect, AP info, Net Toolkit, Scan Hosts, Listen/Client TCP, SOCKS4, Telnet, File Share, Dead Drop, LAN Watch, PineAP Hunter, open networks, AP detail, Roku, WireGuard, Printer, UPnP list, HoneyPot; Loot Console, Check Handshakes, NTLM Clean; SMB Status, SIP OPTIONS, LDAP Read (shared tools; NetOps also has UART Shell) |
| **BLE** | Scan, Hunter, Sniffer UI, Wall Of Flipper / Airtag, Skimmer Detector, Disconnect; Ninebot / Amiibolink / PN532 / Chameleon status (connect/read only) |
| **Ethernet** | Link status, Scan Hosts (onboard NIC) |
| **Infrared** | Use, Learn, Browse (Flipper `.ir` library), Read, Favorites, Config (library folder, onboard TX GPIO12 / RX GPIO13 test); TV-B-Gone, TagTinker ESL, IR Carrier; Roku Remote |
| **Files** | Browse home / mounts, text preview, confirmed delete; File Share stays under Wi-Fi |
| **GPS** | ADV menu: **Wardriving** (Wi-Fi / BLE / Both → live table + WiGLE CSV; View Captures; WD Config; Master stub), **Site Survey**, **GPS Tracker** (GPX), **Config** (hardware Cap vs Unit GPS v1.1 / source / NMEA path / baud / live Status / captures folder). Prefs under `~/.config/kvxputerzero/gps/` (`hardware.txt`: `unit_v11` or `cap_lora`). Both use `/dev/serial0` @ 115200; Unit enables Grove UART+5V (and prefers NMEA over gpsd), Cap enables `ext_5v_out`. Disable `scrolld` grove-setup if Unit GPS shares the Grove port. Desktop shows “No GPS on desktop” |
| **Scripts** | Local allowlist runner under `~/.config/kvxputerzero/scripts/`; PC Connect script browser + how-to |
| **PC Connect** | How to use / what you need; browse bundled host scripts; export to `~/.config/kvxputerzero/pcconnect/` (for a Cardputer running kvxputer PC Connect — not on Zero) |
| **Modules** | Cap Probe, Bus / I2C scan, Unit Scroll, UART Shell, PaHub scan, iButton, PN532Killer UART, UART LLM, Joystick presence |
| **RF** | Cap-CC1101 status, spectrum, listen, record `.sub`, library / replay, test TX (no jammer/bruteforce) |
| **NRF24** | Info, status, spectrum, listen, send payload (no MouseJack / jammer) |
| **LoRa** | Cap-SX1262 status, chat, antenna check, frequency / username; Meshtastic Nodes/Chat over USB CDC PhoneAPI (or TEXTMSG when `lora/mesh_mode.txt` is `text`), else one clear “no mesh serial API” row |
| **RFID** | Cap ST25 / PN532 status, read/scan UID, load/save dumps |
| **NFC** | ADV menu parity: Read/Scan/Load/Erase; Classic (dict/magic/dump editor; Detect Reader/MFKey stubs); Tags (ISO15693/DESFire/FeliCa/forced type; UL-C stub); NDEF write/emulate/records/save-as/memory/chip/lock/password/format/APDU; Lab (APDU script, compare, field poll); Parsers; Companions (EMV on PN532, SRIX gate, BLE/UART status links); Config (Auto/ST25/PN532, wiring, MIF keys). Dumps under `~/.config/kvxputerzero/nfc/` (`.nfc` / `.rfid`) |
| **FM** | Si4713 status, broadcast, stop, spectrum (no TA hijack) |
| **Discovery** | Filterable index of every registered tool |
| **Clock** | Live face (digital / charge-style); Enter → Timer |
| **Charge** | Charge-style face, color cycle, backlight steps |
| **Config** | Colors, brightness, dim time, sound, power (reboot / off), About |
| **USB** | Placeholder (ESP HID / gadget unavailable) |

Optional Cap SPI/I2C paths: `~/.config/kvxputerzero/spi_cc1101.txt`, `spi_nrf24.txt`, `spi_sx1262.txt`, `i2c_si4713.txt`, `i2c_pn532.txt`. Cap radios default to **`/dev/spidev0.1`** (CE1). Cap-CC1101 NFC is **ST25R3916** on the same SPI (mode 1, 5 MHz) with soft-CS **GPIO22** / IRQ **GPIO23** on Zero (Cap docs Adv map is G6/G4). Card Emulation is NFC-A Passive Target on the Cap PCB coil (not the RP-SMA). Bring-up enables `/sys/class/leds/ext_5v_out`, GPIO26 POWER_EN/RST, CC1101 RF_SW0 (GPIO14), and Cap-LoRa PI4IOE@0x43 antenna (P0). Keep the user in `spi`/`i2c`/`gpio`. Do not hot-plug Caps.

Meshtastic serial prefs (under `~/.config/kvxputerzero/lora/`): `mesh_port.txt` (e.g. `/dev/ttyACM0`), optional `mesh_baud.txt`, and `mesh_mode.txt`=`text` for TEXTMSG line mode. Cap-LoRa GPS UART (`/dev/serial0`) is not used as a mesh API.

Theme colors, PDA prefs, and tool data live under `~/.config/kvxputerzero/` (on device or desktop). Network / BLE / audio helpers need the matching host tools (`nmcli`, `bluetoothctl`, `aplay`/`arecord`, …); desktop builds show a clear error instead of hanging.

### Tools briefly

1. Open **Tools** → pick Calculator, Calculators, Codec / Hash, Passgen, TOTP, Combo, QRCodes, Barcode, PDA, Media Player, Timer, Matrix, DTMF, Morse, Microphone, MQTT, LLM, or Drone ID. Matrix plays the boot rain for 5 seconds; any key leaves it early.
2. **Calculator**: type an expression (`sin(90)`, `2^10`, `ans*2`). **Enter** evaluates; empty field + **D** toggles DEG/RAD. PDA → Calc opens the same screen.
3. On Codec/engineering-Calculators/Combo work screens: type input in the field, press **Enter** to run, **Esc** to go back to that tool’s menu (then Esc again to Tools).
4. **Calculator / Calculators:** type normally; ops strip inserts `.` `/` `-` `+` … (**↑** / **F** focuses it, **← → Enter** / **Z C Enter** inserts). Eng calcs: bare **F/X/Z/C** = Up/Down/Left/Right (no Fn). After **Enter**, **F/X** scroll the result. DEG/RAD is the first chip on Calculator.
5. **Passgen**: **Z/C** length, **F/X** count, **Enter** / **Sym+G** generate, **Sym+U/L/D/C** charsets, **Sym+S** append to `~/.config/kvxputerzero/passwords.txt`.
6. **TOTP**: accounts stored plaintext in `~/.config/kvxputerzero/totp.json` (mode 0600). Needs correct system time. **Z/C** switch accounts.
7. **QR / Barcode**: Enter on the work screen draws the glyph; **Space** inverts QR; Esc returns to the tool menu. Type **B** for `.` in URLs / IPs.
8. **PDA**: 4×2 hub (Notes, Memos, To-Do, Calendar, Contacts, Alarms, World Clock, Calculator). **F/X/Z/C** move; Enter opens; keys **1–8** open the bound channel; **9** rebinds. Calculator tile opens the in-app scientific Calculator. Data lives under `~/.config/kvxputerzero/pda/` (notes/, memos.txt, todo.txt, calendar/events.txt, contacts.txt, alarms.txt). Lists: **Del** deletes; **8** / **Sym+N** new. Calendar: **Z/C** day, **F/X** week, **L/M** month, **-/+** (bare R/T) year. World Clock: **Z/C** cycle faces; Enter sets app TZ offset (not OS timezone); **Sym+Y** toggles 12/24h. Alarms fire only while this app is open (about every 15s); any key dismisses.
9. **Run screens** (scans, shares, BLE, UART, IR, media, …): Esc cancels the worker and returns. Optional save writes under `~/.config/kvxputerzero/scans/`.

---

## Desktop emulator (PC / laptop)

Use this to try the UI without a Cardputer Zero. You need a graphical session (a normal desktop; `DISPLAY` set on Linux).

### Requirements

- CMake **≥ 3.31**
- **Ninja** (Multi-Config generator; install `ninja` / `ninja-build`)
- **SDL2**, PNG, FreeType (and a C++17 compiler)

On Arch-like systems, for example:

```bash
sudo pacman -S cmake ninja sdl2 libpng freetype2 gcc
```

### Build and run (Linux)

```bash
cd kvxputerzero/app

cmake --preset linux-x86-64
cmake --build --preset linux-x86-64-dbg

./build/linux-x86-64/Debug/kvxputerzero
```

Release build:

```bash
cmake --build --preset linux-x86-64-rel
./build/linux-x86-64/Release/kvxputerzero
```

### macOS

```bash
cmake --preset darwin-arm64    # or darwin-x86-64
cmake --build --preset darwin-arm64-dbg
./build/darwin-arm64/Debug/kvxputerzero
```

### Optional: auto screenshot then exit

```bash
DISPLAY=:0 KVX_SCREENSHOT_PATH=screenshot/linux_screenshot.png \
  ./build/linux-x86-64/Debug/kvxputerzero
```

---

## Install on Cardputer Zero

Cardputer Zero is **aarch64 Linux**. Build the `.deb` on a PC with a cross-compiler — **do not** compile this app on the device (512 MB RAM; easy to OOM).

### 1. Host requirements

- `aarch64-linux-gnu-g++` (Arch: `aarch64-linux-gnu-gcc`; Debian/Ubuntu: `g++-aarch64-linux-gnu`)
- Same CMake / Ninja as above
- Network on first configure (dependencies are fetched into `.cache/`)

If the Release build dies inside `<cmath>` / `bits/mathcalls.h` with `__intmax_t` errors, you are mixing Arch host cross headers with the CM0 BSP. Reconfigure after pulling the toolchain fix in `cmake/toolchains/cp0-aarch64-linux-gnu.cmake` (`rm -rf build/cp0-cross` then rerun the workflow).

### 2. Build the arm64 package

From `kvxputerzero/app`:

```bash
# One-shot: configure, Release build, package → dist/*.deb
cmake --workflow --preset cp0-cross-package
```

Or step by step:

```bash
cmake --preset cp0-cross
cmake --build --preset cp0-cross-rel
cmake --build --preset cp0-cross-rel --target package
```

The Debian package appears as:

```text
dist/kvxputerzero_<version>_rev4_arm64.deb
```

### 3. Copy the package to the device

**Option A — helper script** (builds via the workflow, then `scp`):

```bash
REMOTE_USER=pi REMOTE_HOST=192.168.1.42 ./deploy.sh
```

Replace `pi` / IP with your Cardputer Zero SSH user and address. The `.deb` lands in that user’s home directory by default (`REMOTE_DIR` overrides).

**Option B — manual copy**

```bash
scp dist/kvxputerzero_*_arm64.deb pi@CARDPUTER_IP:~/
```

### 4. Install on the Cardputer Zero

SSH in, then:

```bash
ssh pi@CARDPUTER_IP
sudo apt install ./kvxputerzero_*_arm64.deb
```

Or:

```bash
sudo dpkg -i kvxputerzero_*_arm64.deb
sudo apt-get install -f   # only if dpkg reports missing deps
```

### 5. Launch on device

Open **APPLaunch** (the Cardputer Zero launcher) and start **kvxputerzero**.  
Or from a shell:

```bash
kvxputerzero
```

Keyboard input uses the device keypad (evdev). Hold **Esc** on the channel home to quit.

### Update / remove

After a Tools feature drop, rebuild the arm64 package and reinstall on device (same package name):

```bash
# On the build host
cmake --workflow --preset cp0-cross-package
# or: ./deploy.sh

# On the Cardputer Zero
sudo apt install ./kvxputerzero_*_arm64.deb

# Remove
sudo apt remove kvxputerzero
```

Re-check the keypad after each install (evdev / tca8418 grab).

---

## App Store publish (optional)

Publishing to the Cardputer Zero Store is separate from installing on your own device. Metadata lives in [`app-builder.json`](app-builder.json). Typical flow uses [AppBuilder](https://github.com/CardputerZero/AppBuilder) / `czdev` after a validated `cp0-cross-package` build. `deploy.sh` only copies a `.deb` to a machine; it does **not** publish to the Store.

---

## Project map (for developers)

| Path | Role |
| --- | --- |
| `src/model/channel_registry.h` | Channel IDs / labels / kinds |
| `src/model/tool_registry.h` | Shared tool ids across channels |
| `src/app/tool_screens.*` | Channel tool menus + every ToolId screen |
| `src/platform/` | net / wifi / ble / tcp / dns / http / audio / i2c / uart helpers |
| `src/view/screens/channel_grid_screen.*` | 3×2 home |
| `src/tools/` | Codec / EngCalc / PDA / … logic |
| `app-builder.json` | Store listing |
| `deploy.sh` | Cross package + `scp` to device |

Audit / wave planning for more channels: [`../README.md`](../README.md), [`../AUDIT.md`](../AUDIT.md), [`../WAVES.md`](../WAVES.md).
