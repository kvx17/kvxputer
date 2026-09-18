# kvxputer Architecture

## Three-root model

kvxputer separates concerns into three roots on both the **device filesystem** and the **source tree**.

| Root | Purpose | On device | In repo |
|------|---------|-----------|---------|
| `root/` | System: boot, config, UI framework, HAL | `/root/` | `src/root/` |
| `menu/` | Feature code per main-menu app | `/menu/` (user scripts) | `src/menu/<domain>/` |
| `support_files/` | Runtime assets (profiles, captures) | `/support_files/` | `data/support_files/` |

## On-device layout

```
/
├── root/
│   ├── kvxputer.conf
│   ├── boot.wav
│   ├── themes/
│   └── keys/mifare.keys
├── menu/
│   └── scripts/          # user .bjs scripts
└── support_files/
    ├── wifi/
    ├── rfid/
    ├── infrared/
    ├── rf/
    ├── lora/
    ├── netops/
    ├── media/
    ├── pda/
    └── gps/
```

Firmware uses a three-root on-device layout. Optional development reference lives in gitignored `resources/` — see [REFERENCE.md](REFERENCE.md). Large assets and companion `.bin` files go on SD (`tools/sd_pack/`); companion source is in `tools/companions/`. The default boot GIF is linked into the firmware image from `tools/build/embedded_resources/bootimage.gif` (Cardputer / StickS3) and does not need to be copied to the SD card.

## Source layout

```
src/
├── main.cpp
├── root/
│   ├── app/       startup, power
│   ├── ui/        display, theme, menu framework
│   ├── config/    KvxputerConfig, pins
│   ├── hal/       board HAL
│   ├── storage/   SD/LittleFS, paths
│   ├── net/       shared WiFi infra
│   ├── input/     keyboard, unit scroll
│   ├── serial/    CLI
│   └── scripting/ BJS interpreter engine
└── menu/
    ├── wifi/
    ├── netops/
    ├── ble/
    └── ...        one folder per main-menu domain
```

## Module rules

1. `*_menu.cpp` files only build option lists and call feature entry points.
2. One feature ≈ one `.cpp/.h` pair.
3. No cross-domain includes between `menu/*`; shared code goes in `root/`.
4. All filesystem paths use `kvx::paths::*`.
5. `media` is support-only under `/support_files/media/`.

## LittleFS seed

Factory defaults ship via PlatformIO `data/`:

```bash
pio run -e m5stack-cardputer -t uploadfs
```

## Attribution

Firmware architecture derived from [Bruce](https://github.com/BruceDevices/Firmware) (AGPL-3.0). See [NOTICE](../NOTICE).
