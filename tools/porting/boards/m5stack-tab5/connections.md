# M5Stack Tab5 — kvxputer connections (milestone 1)

Device docs: https://docs.m5stack.com/en/core/Tab5  
Keyboard: https://docs.m5stack.com/en/tab5/Tab5_Keyboard (SKU A164)

## Buses

| Bus | Pins | Use |
| --- | --- | --- |
| Sys I2C (`M5.In_I2C`) | G31 SDA, G32 SCL | Touch, BMI270, RX8130, INA226, PI4IO, codecs |
| Keyboard I2C | G0 SDA, G1 SCL, G50 INT | Tab5 Keyboard @ 0x6D (Character mode) |
| Grove PORT.A | G53 SDA/TX, G54 SCL/RX | Unit Scroll, Joystick2, PaHub, RFID2, PN532 I2C, GPS UART, IR/RF Grove |
| microSD SPI | MISO G39, CS G42, SCK G43, MOSI G44 | Files (SPI mode — not SDIO; C6 uses SDIO) |
| M5-Bus SPI | SCK G5, MOSI G18, MISO G19 | Optional CC1101 / NRF24 (CS defaults to -1) |
| Display (MIPI-DSI) | DSI lanes dedicated; LEDA **G22** PWM | M5GFX `Bus_DSI` + `Panel_ST7121` / `Panel_ST7123` / `Panel_ILI9881C` (M5Unified ≥0.2.23, M5GFX ≥0.2.30). Native panel 720×1280; default UI rotation **3** → 1280×720 with keyboard along the bottom (flip via Settings → Orientation). |
| C6 SDIO host | CLK G12, CMD G13, D0–D3 G11/G10/G9/G8, RST G15 | Hosted WiFi (STA/AP) and hosted BLE HCI (`BleBackendHostedC6`) |

## EXT 5V

PI4IOE5V6408-1 (0x43) P2 `EXT5V_EN` feeds HY2.0-4P, M5-Bus, and the side header.  
Infrared menu enables it via `M5.Power.setExtOutput(true)` while open.  
Keyboard uses 3.3V on Ext.Port1 and does not need EXT 5V.

## Suggested M5-Bus radio wiring (optional — set in Pins menu)

| Device | CS | GDO0 / CE |
| --- | :---: | :---: |
| CC1101 | G16 | G45 |
| NRF24 | G2 | G3 |

Defaults in firmware: CS / GDO0 / CE = **-1** (not driven at boot).

## Module matrix (probe-at-runtime)

| Module | Port | Notes |
| --- | --- | --- |
| Unit Scroll | Grove I2C | One accessory at a time on PORT.A |
| Joystick2 | Grove I2C | |
| PaHub | Grove I2C | RFID / Scroll on hub channels |
| RFID2 / PN532 I2C | Grove or PaHub | |
| GPS | Grove UART (G53/G54) | Not simultaneous with I2C on PORT.A |
| IR TX/RX | Grove | TX=G53, RX=G54 (pin table) |
| RF 433 single-pin | Grove | Same pin table |
| Si4713 FM | Grove I2C | `FM_SI4713=1`, RST=-1 |
| CC1101 / NRF24 | M5-Bus SPI | Configure CS in Pins |

## Radios

Catalog matches Cardputer (`EVIL_EXTENSIONS`, no lite).  

- **WiFi:** STA/AP and Evil Portal run on the hosted C6. `esp_wifi_80211_tx`, promiscuous, CSI, and ESP-NOW are weak `esp_wifi_remote_*` stubs in pioarduino 55.03.39 (`ESP_ERR_NOT_SUPPORTED`). Those menu entries stay visible and fail closed with that reason. No external WiFi inject module is wired.
- **BLE:** `BleBackendHostedC6` — `hostedInitBLE()` + IDF NimBLE host over ESP-Hosted VHCI (`CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE`). GAP scan and non-connectable advertise are implemented. GATT client/server and HID are not; those entries name `needs hosted C6 BLE GATT`. NimBLE-Arduino is not linked.
- **External BLE UART/SPI:** not selected. Onboard/hosted C6 covers GAP. A Grove UART bridge would be a new `-DKVX_BLE_BACKEND=...` only if a later app needs GATT the C6 host cannot do. Do not use keyboard G0/G1, sys I2C G31/G32, C6 SDIO, SD SPI, or backlight G22.

Hosted C6 STA uses `WiFi.setPins` + settle before `WiFi.mode`.  
UI stays direct-to-panel (no 720p frame canvas) — canvas blit + SDIO races paint cyan.
