# Local reference material (`resources/`)

The `resources/` directory is **gitignored**. It is a **development-only** snapshot for comparing ports. Firmware never compiles it, includes it, or opens it at runtime.

A clone without `resources/` must still **build and run**.

| Path | Purpose |
|------|---------|
| `resources/Evil-Cardputer-v1-5-4.ino` | Evil-Cardputer v1.5.4 — compare while porting |
| `resources/Evil-M5Project-main/` | Other-device sketches and slave `.ino` (optional local copy) |
| `resources/sd_files/` | Example SD content (themes, IR libraries) |

If `resources/` is missing after clone, firmware is unchanged. Companion sketches live in tracked [`tools/companions/`](../tools/companions/). Runtime files use [`kvx::paths`](../src/root/storage/paths.h) on SD or LittleFS — never `resources/`.

## Storage

| Store | Size (Cardputer ADV) | Use |
|-------|----------------------|-----|
| App flash | ~4.9 MB | Firmware (`m5stack-cardputer`) |
| LittleFS | 3 MB | Small factory seed via `pio run -e m5stack-cardputer -t uploadfs` ([`tools/build/data/`](../tools/build/data/)) |
| SD card | user | Large wordlists, ESL bitmaps, IR libraries, [companion `.bin`](../tools/sd_pack/README.md) |

[`getFsStorage()`](../src/root/storage/sd_functions.cpp) prefers a mounted SD card, then LittleFS. Missing optional files must show an error, not crash.

Copy [`tools/sd_pack/support_files/`](../tools/sd_pack/README.md) to the card root as `/support_files/`.

Active firmware: `src/`, `tools/porting/boards/`, `tools/build/data/`, `tools/companions/`, `platformio.ini`.
