#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include "soc/soc_caps.h"
#include <stdint.h>

// M5Stack Tab5 (ESP32-P4 app MCU + ESP32-C6 wireless over SDIO).
// Keyboard accessory: I2C 0x6D on G0/G1, INT G50 — not Grove PORT.A.

#define USB_VID 0x1209
#define USB_PID 0x0001
#define USB_MANUFACTURER "Generic"
#define USB_PRODUCT "HID Keyboard"
#define USB_SERIAL "1"

#define HAS_KEYBOARD // Tab5 Keyboard accessory (Character mode → KeyStroke)
#define HAS_TOUCH 1

// 720p readability — define here (not -DFP= build flag; that breaks M5Utility DES / FastLED)
#define FP 4
#define FM 5
#define FG 6

static const uint8_t TX = 37;
static const uint8_t RX = 38;

// Default Arduino Wire aliases → Grove PORT.A (HY2.0-4P)
static const uint8_t SDA = 53;
static const uint8_t SCL = 54;

// CH9329 BadUSB UART (Grove) when not using native USB_as_HID gadget
#ifndef BAD_TX
#define BAD_TX 53
#endif
#ifndef BAD_RX
#define BAD_RX 54
#endif

// M5-Bus SPI (CC1101 / NRF24 when CS is configured at runtime)
static const uint8_t SS = 16;
static const uint8_t MOSI = 18;
static const uint8_t MISO = 19;
static const uint8_t SCK = 5;

static const uint8_t G0 = 0;
static const uint8_t G1 = 1;
static const uint8_t G2 = 2;
static const uint8_t G3 = 3;
static const uint8_t G4 = 4;
static const uint8_t G5 = 5;
static const uint8_t G6 = 6;
static const uint8_t G7 = 7;
static const uint8_t G8 = 8;
static const uint8_t G9 = 9;
static const uint8_t G10 = 10;
static const uint8_t G11 = 11;
static const uint8_t G12 = 12;
static const uint8_t G13 = 13;
static const uint8_t G14 = 14;
static const uint8_t G15 = 15;
static const uint8_t G16 = 16;
static const uint8_t G17 = 17;
static const uint8_t G18 = 18;
static const uint8_t G19 = 19;
static const uint8_t G20 = 20;
static const uint8_t G21 = 21;
static const uint8_t G22 = 22;
static const uint8_t G23 = 23;
static const uint8_t G26 = 26;
static const uint8_t G27 = 27;
static const uint8_t G28 = 28;
static const uint8_t G29 = 29;
static const uint8_t G30 = 30;
static const uint8_t G31 = 31;
static const uint8_t G32 = 32;
static const uint8_t G34 = 34;
static const uint8_t G35 = 35;
static const uint8_t G37 = 37;
static const uint8_t G38 = 38;
static const uint8_t G39 = 39;
static const uint8_t G40 = 40;
static const uint8_t G41 = 41;
static const uint8_t G42 = 42;
static const uint8_t G43 = 43;
static const uint8_t G44 = 44;
static const uint8_t G45 = 45;
static const uint8_t G47 = 47;
static const uint8_t G48 = 48;
static const uint8_t G50 = 50;
static const uint8_t G51 = 51;
static const uint8_t G52 = 52;
static const uint8_t G53 = 53;
static const uint8_t G54 = 54;

// Tab5 Keyboard Ext.Port1
#define TAB5_KB_SDA 0
#define TAB5_KB_SCL 1
#define TAB5_KB_INT 50
#define TAB5_KB_I2C_ADDR 0x6D

// ESP32-C6 hosted WiFi (SDIO) — official Tab5 pin map
#define BOARD_HAS_SDIO_ESP_HOSTED
#define BOARD_SDIO_ESP_HOSTED_CLK 12
#define BOARD_SDIO_ESP_HOSTED_CMD 13
#define BOARD_SDIO_ESP_HOSTED_D0 11
#define BOARD_SDIO_ESP_HOSTED_D1 10
#define BOARD_SDIO_ESP_HOSTED_D2 9
#define BOARD_SDIO_ESP_HOSTED_D3 8
#define BOARD_SDIO_ESP_HOSTED_RESET 15

#endif /* Pins_Arduino_h */
