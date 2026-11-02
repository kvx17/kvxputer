# Live-screen jitter — results and on-device checklist

Primary verify board: **M5Stack Cardputer** (`pio run -e m5stack-cardputer`). Serial: type `uimetrics on` before opening a screen.

## Build status

| Env | Status | Notes |
|---|---|---|
| m5stack-cardputer | **pass** | Primary target. No new warnings from this work. |
| m5stack-sticks3 | **pre-existing fail** | WireGuard-ESP32 / LibSSH undefined refs at link. Display (M5GFX) objects compiled. |
| m5stack-tab5 | **pre-existing fail** | `ble_spam.cpp` NimBLE API mismatch. Unrelated to paint changes. |

## Results table (code-derived rates; device = pending)

A fix counts when painted rects shrink or paint less often in code, heap stays inside the 8 KB steady budget, and radio dwells/hop rates are untouched. **Device** stays pending until the checklist below is run on hardware.

| Screen | Cause | Fix | paints/s before | paints/s after | area before | area after | heap delta | device |
|---|---|---|---|---|---|---|---|---|
| Shared `drawKvRow` | 2 clear-then-draw | Opaque glyphs + tail fill | 1 full-row clear / call | glyph width + tail | ~240×12 | ~used×12 | 0 | pending |
| Top bar | 5 every caller | Skip if title/status/icons unchanged; bat/clock 1 s | every call | on real change | 240×24 | 0 or 240×24 | 0 | pending |
| Scanner list rows | 2 | Opaque row; fill only on selection change | every dirty row clear | opaque / sel fill | 240×rowH | less | 0 | pending |
| AirTag list | 7 sort every 220 ms | Sort hold 2 s | most rows dirty | few | body | body | 0 | pending |
| ScrollableTextArea | 1 viewport clear | Line patch when scroll stable | full viewport | dirty lines | viewport | lines | 0 | pending |
| WiFi analyzer bars | 5+8 | Chrome once; dirty columns only | 11 cols / dwell | dirty cols | ~224×89 | 1 col | 0 | pending |
| Jam Detect | 1+3+8 | Banner/rows incremental; hop = 2 rows | ~10 full body/s | 2 rows / hop | ~body | 2×row | 0 | pending |
| CSI Radar | 1+4+8 | Column delta | 12.5–20 full band/s | dirty cols | 70×(W-16) | col | 0 | pending |
| NRF spectrum | 1+4+8 | Radio walk full rate; paint ≤30 fps, plot band only | ~1000×80 vlines | ≤30 dirty cols | full H | plot H | 0 | pending |
| RF spectrum | 1+4+8 | Queue drain 10 ms; paint ~15 fps column erase | per burst full plot | 15 fps cols | plot | cols | 0 | pending |
| RF waterfall | 3+6 | Chrome once; no full-height grid restamp | every sweep | row + rare chrome | panel H | 1 row | 0 | pending |
| Mic spectrum | 8+4 | Column push ~20 fps (~250 B) | full FB / FFT | 1 col | ~57 KB blit | 1×H | −57 KB peak | pending |
| Deauth flood | 1+2+5 | LiveField; no border every 2 s | 0.5 fillScreen/s | 2 fields | full | 2 rows | 0 | pending |
| Handshake master | 2+3+4 | Paint on `gRxCount` change | 25/s | on change | row | row | 0 | pending |
| Probe sniff | 2+3+4 | Paint on count change | 12.5/s | on change | row | row | 0 | pending |
| Netcut attack | 2+3 | Skip when counts unchanged | 2/s footer clear | on change | footer | rows | 0 | pending |
| Wardriving HUD | 1+3+4+5 | Chrome once; dirty lines | ~full screen / loop | dirty lines | 240×135 | lines | 0 | pending |
| GPS tracker | 1+2+3+5 | Soft STA draw; top-bar cache | 2 body/s | dirty lines | viewport | lines | 0 | pending |
| Karma HUD | 1+2+3 | Skip body wipe if counters unchanged | 1 body/s | on change | body | body | 0 | pending |
| LoRa antenna | 1+3+4 | LiveField lines | 12.5 fillScreen/s | dirty lines | full | lines | 0 | pending |
| Mesh nodes | 1+3+5 | LiveField; chrome once | 2 fillScreen/s | dirty lines | full | lines | 0 | pending |
| BLE Hunter | 1+3 | LiveField; banner only on change | ~1 body/s | fields | body | fields | 0 | pending |
| Tag-O-Matic scan | 1+3+4 | Banner only on new UID | ~5 fillScreen/s | on new | full | full | 0 | pending |
| Charge clock/mV | 2 | Opaque `drawField` | 1 band clear/s | opaque | band | glyphs | 0 | pending |
| Clock digital | 2 | Opaque centre string | 1 band clear/s | opaque | band | glyphs | 0 | pending |
| Pineap list | 3 | Ignore `totalScans` as redraw reason | every scan | visual change | body | body | 0 | pending |
| FindMy / name flood / SSDP / WD master / EAP | 2+3 | Drop pre-clear; paint on change where needed | loop rate | on change | row | row | 0 | pending |

Radio periods left unchanged: `JD_DWELL` 100 ms, analyzer dwell (default 350), NRF `delayMicroseconds(128)`, sniffer `vTaskDelay(100)`, CSI `delay(50/80)`, wardriving `scan_delay_ms`.

## On-device checklist (Cardputer)

1. Flash `m5stack-cardputer`. Serial monitor open.
2. Send `uimetrics on`.
3. For each item: open → leave ~10 s → Esc → copy the 1 Hz `[uimetrics]` lines (paints/s, avgPx, maxPx, heap, dma, fallbacks).

Watch for:

| Open | Watch |
|---|---|
| WiFi → kvx wifi analyzer | Bars move; top bar does not flash; SSID list does not reorder alone |
| WiFi → Jam Detect | Hopping does not wipe the page; active channel row updates |
| WiFi → Sniffer | Log does not blank once a second |
| WiFi → Deauth Flood / Karma / Netcut / Probe sniff / Handshake Master | Counters change in place |
| BLE → BLE Scan / Wall of Flipper / AirTag / Skimmer | Selection moves one row; AirTag order holds ~2 s |
| NRF → NRF Spectrum; RF → Spectrum / Waterfall | No full-screen flash; waterfall history not sliced |
| GPS → Wardriving / GPS Tracker | Clock/status updates; rest stays put |
| Clock / Charge | Seconds digit changes without blanking the whole time band |
| Tools → Mic Spectrum | Scrolls without `Not Enough RAM` |

Heap: entry, steady (~2 s), exit within **8 KB** of entry on the no-PSRAM path. `beginFrame` fallbacks during a radio scan are expected.
