# kvxputerzero — user guide

**kvxputerzero** is a Cardputer Zero app: a Wii-style channel home (like Cardputer kvxputer) with Tools, Config, and placeholders for later channels.

On launch it plays the same Cardputer intro GIF (`assets/images/boot.gif`); **press any key to skip**. APPLaunch icons are derived from that intro frame.

This guide covers **how to use the app**, **how to run the desktop emulator**, and **how to build and install it on a Cardputer Zero**.

---

## How to use

### Channel home

On launch you get a **3×2** grid of channels (purple tiles, green selection border). The footer shows the selected channel name and page (`1/4`, etc.).

The bottom bar is the Cardputer Zero **4 5 6 7 8** row. The green number is the key; the icon is what it does on this screen.

| Control | Action |
| --- | --- |
| **F / X / Z / C** | Up / down / left / right |
| **Fn + letter** | Type that letter (on a PC keyboard, hold **Left Alt** as Fn) |
| **4 / 8** on the channel home | Previous / next page |
| **5 / 7 / 6** on the channel home | Up / down / open |
| **4 / 5 / 6 / 7 / 8** on a menu | Back / up / select / down / home |
| **Enter** or **Space** | Open or activate |
| **Esc** | Go back one screen |
| **Hold Esc** (on home) | Quit the app |
| **H** | Keyboard help overlay (**Fn+H** types h) |
| **J** | Save a screenshot (**Fn+J** types j) |

Keys that have an orange Fn legend on the Cardputer Zero use that legend by themselves. **Fn** types the printed letter instead. Number keys stay numbers, so **4–8** keep driving the bar; **Fn+1** through **Fn+0** are still F1–F10. Letters with no legend (`g r t y v`) type normally.

### What’s available now

| Channel | What you get |
| --- | --- |
| **Tools** | Codec / Hash, Engineering Calculators, Passgen, TOTP, Combo, QRCodes, Barcode, **PDA**, Timer |
| **Clock** | Timer (countdown; adjust with ←/→ or `+`/`-`, Enter to start/pause) |
| **Config** | UI color, accent color, dark mode, About |
| Other tiles | Short “Coming soon” or “Use stock …” message (stock OS/Cap apps cover those domains) |

Theme colors and PDA prefs are saved under `~/.config/kvxputerzero/` (on device or desktop).

### Tools briefly

1. Open **Tools** → pick Codec / Hash, Calculators, Passgen, TOTP, Combo, QRCodes, Barcode, PDA, or Timer.
2. On Codec/Calc/Combo work screens: type input in the field, press **Enter** to run, **Esc** to go back to that tool’s menu (then Esc again to Tools).
3. Calculators expect formats shown in the on-screen hint (e.g. subnet `192.168.1.10/24`, or a mode token first).
4. **Passgen**: `;`/`.` length, `,` count, `U`/`L`/`D`/`C` charsets, `G` generate, `S` append to `~/.config/kvxputerzero/passwords.txt`.
5. **TOTP**: accounts stored plaintext in `~/.config/kvxputerzero/totp.json` (mode 0600). Needs correct system time.
6. **QR / Barcode**: Enter on the work screen draws the glyph; **Space** inverts QR; Esc returns to the tool menu.
7. **PDA**: 4×2 hub (Notes, Memos, To-Do, Calendar, Contacts, Alarms, World Clock, Calculator). Arrows move; Enter opens; keys **1–8** open the bound channel; **9** rebinds. Calculator tile points at the stock Calculator app. Data lives under `~/.config/kvxputerzero/pda/` (notes/, memos.txt, todo.txt, calendar/events.txt, contacts.txt, alarms.txt). Calendar: ←/→ day, ↑/↓ week, `[` `]` month, `;` `.` year. World Clock: `[` `]` cycle cities / charge / digital faces; Enter sets app TZ offset (not OS timezone); **H** toggles 12/24h. Alarms fire only while this app is open (about every 15s); any key dismisses.

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
dist/kvxputerzero_<version>_rev1_arm64.deb
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
| `src/model/channel_registry.h` | Channel IDs / labels |
| `src/view/screens/channel_grid_screen.*` | 3×2 home |
| `src/tools/` | Codec / EngCalc logic |
| `app-builder.json` | Store listing |
| `deploy.sh` | Cross package + `scp` to device |

Audit / wave planning for more channels: [`../README.md`](../README.md), [`../AUDIT.md`](../AUDIT.md), [`../WAVES.md`](../WAVES.md).
