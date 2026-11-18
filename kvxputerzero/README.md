# kvxputerzero

Cardputer Zero **AppStore shell** plus the Cardputer kvxputer portability audit.

| Path | Role |
| --- | --- |
| [`app/`](app/) | Single package **`kvxputerzero`**: Wii-style 3×2 channel grid; Tools + WiFi/NetOps/BLE/Ethernet/GPS/Scripts/**PC Connect**/Modules/Discovery filled; Config theme; stock/placeholder tiles for Cap/OS domains |
| [`AUDIT.md`](AUDIT.md) | Per-leaf verdicts for the ESP Cardputer firmware tree |
| [`WAVES.md`](WAVES.md) | Wave catalog + shipped status for channel tools |

Later plans only add channel screens under `app/src/` — Store identity stays **`kvxputerzero`**.

## App user guide

**How to use the UI, run the desktop emulator, and install on Cardputer Zero:**  
→ **[`app/README.md`](app/README.md)**

Store metadata: [`app/app-builder.json`](app/app-builder.json).

## Compile

All builds run from `kvxputerzero/app` (CMake presets live there). Needs CMake ≥ 3.31 and Ninja.


### Desktop emulator (x86_64 Linux)

```bash
cd kvxputerzero/app
cmake --preset linux-x86-64
cmake --build --preset linux-x86-64-dbg
./build/linux-x86-64/Debug/kvxputerzero
```

Release: `cmake --build --preset linux-x86-64-rel` then run `./build/linux-x86-64/Release/kvxputerzero`.

### Cardputer Zero package (aarch64 `.deb`)

Cross-compile on a PC — do not build on the device. Needs `aarch64-linux-gnu-g++` (Arch: `aarch64-linux-gnu-gcc`; Debian/Ubuntu: `g++-aarch64-linux-gnu`).

```bash
cd kvxputerzero/app
cmake --workflow --preset cp0-cross-package
```

The package lands at:

```text
dist/kvxputerzero_<version>_rev1_arm64.deb
```

Step-by-step equivalent:

```bash
cmake --preset cp0-cross
cmake --build --preset cp0-cross-rel
cmake --build --preset cp0-cross-rel --target package
```

## Copy to the device (SSH)

Replace `USER` and `HOST` with your Cardputer Zero SSH login (example: `kvx@192.168.50.153`).

### Manual `scp`

```bash
cd kvxputerzero/app
scp dist/kvxputerzero_*_arm64.deb USER@HOST:~/
```

Then install over SSH:

```bash
ssh USER@HOST
sudo apt install ./kvxputerzero_*_arm64.deb
```

Or one-liner from the build host after `scp`:

```bash
ssh USER@HOST 'sudo apt install -y ~/kvxputerzero_*_arm64.deb'
```

### Helper script (build + `scp`)

```bash
cd kvxputerzero/app
REMOTE_USER=USER REMOTE_HOST=HOST ./deploy.sh
```

Example:

```bash
REMOTE_USER=kvx REMOTE_HOST=192.168.50.153 ./deploy.sh
```

Then on the device: `sudo apt install ./kvxputerzero_*_arm64.deb`. Launch from APPLaunch or run `kvxputerzero`.

## Audit (still applies)

No stock OS clones, no **blocked** ESP radio/USB ports. Platform delta, SKU split, and verdict legend live in the sections below and in [AUDIT.md](AUDIT.md).

### Platform delta

| | kvxputer today | Cardputer Zero |
| --- | --- | --- |
| SoC | ESP32-S3 | Raspberry Pi CM0 (RP3A0 / BCM2837), aarch64 |
| OS | Arduino / ESP-IDF firmware (one image) | Linux (CardputerZero `pi-gen` image) |
| Display | ~240×135 | ST7789v3 **320×170**, no touch |
| App shape | One PlatformIO binary | arm64 `.deb` + APPLaunch `.desktop` |

There is **no ESP32** on Cardputer Zero. Hardware authority: [CardputerZero docs](https://docs.m5stack.com/en/core/CardputerZero). SDK: [github.com/CardputerZero](https://github.com/CardputerZero).

### Built-in OS / stock apps (do not reimplement)

Stock Cap apps (LoRa/CC1101/NFC/RFID) and Store/Camera/Compass remain external. kvxputerzero now includes its own Calculator, Files browser, Clock/Charge faces, Media Player, IR (NEC/LIRC), and Wi-Fi Connect/Disconnect/AP info inside this package; RF/LoRa/RFID/NFC tiles still show **Use stock**.

### Verdict legend

| Verdict | Meaning |
| --- | --- |
| **rewrite** / **onboard** | Eligible tools for waves |
| **os** | Stock already covers — do not port |
| **accessory** / **blocked** / **asset** / **drop** | Not first-wave code ports |

See [AUDIT.md](AUDIT.md) and [WAVES.md](WAVES.md).
