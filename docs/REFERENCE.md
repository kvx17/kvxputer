# Local reference material (`resources/`)

The `resources/` directory is **gitignored**. It holds inspiration and demo material only — nothing here is compiled into kvxputer firmware.

Keep these files locally for porting and comparison:

| Path | Purpose |
|------|---------|
| `resources/Evil-Cardputer-v1-5-4.ino` | Evil-Cardputer v1.5.4 snapshot |
| `resources/sd_files/` | Example SD/LittleFS assets (themes, portals, IR, scripts) |

If `resources/` is missing after clone, create it and add your own copies of the reference files.

Active firmware lives under `src/`, `tools/porting/boards/`, `tools/build/data/`, and `platformio.ini`.
