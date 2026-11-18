# kvxputerzero

Cardputer Zero **AppStore shell** plus the Cardputer kvxputer portability audit.

| Path | Role |
| --- | --- |
| [`app/`](app/) | Single package **`kvxputerzero`**: Wii-style 3×2 channel grid, Tools (Codec / Eng Calculators / Passgen / TOTP / Combo / QR / Barcode / PDA / Timer), Config theme, channel placeholders |
| [`AUDIT.md`](AUDIT.md) | Per-leaf verdicts for the ESP Cardputer firmware tree |
| [`WAVES.md`](WAVES.md) | Suggested fill order for remaining channels (WiFi, BLE, NetOps, …) |

Later plans only add channel screens under `app/src/` — Store identity stays **`kvxputerzero`**.

## App user guide

**How to use the UI, run the desktop emulator, and install on Cardputer Zero:**  
→ **[`app/README.md`](app/README.md)**

Quick emulator:

```bash
cd kvxputerzero/app
cmake --preset linux-x86-64
cmake --build --preset linux-x86-64-dbg
./build/linux-x86-64/Debug/kvxputerzero
```

Quick device package + copy (needs `aarch64-linux-gnu` toolchain):

```bash
cd kvxputerzero/app
REMOTE_USER=pi REMOTE_HOST=192.168.1.42 ./deploy.sh
# then on device: sudo apt install ./kvxputerzero_*_arm64.deb
```

Store metadata: [`app/app-builder.json`](app/app-builder.json).

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

Launcher, Files, Store, FactoryTest, Camera, Compass, IR-Remote, IR-Chat, Music, Piano, Recorder, Calendar, Calculator, Cap-LoRa-1262-GPS, Cap-CC1101-*, RFID, and ordinary Wi-Fi connect / SoftAP. Channel tiles for those domains show **Use stock** placeholders.

### Verdict legend

| Verdict | Meaning |
| --- | --- |
| **rewrite** / **onboard** | Eligible tools for waves |
| **os** | Stock already covers — do not port |
| **accessory** / **blocked** / **asset** / **drop** | Not first-wave code ports |

See [AUDIT.md](AUDIT.md) and [WAVES.md](WAVES.md).
