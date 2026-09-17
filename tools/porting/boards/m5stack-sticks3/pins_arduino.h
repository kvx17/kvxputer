#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include "soc/soc_caps.h"
#include <stdint.h>

// StickS3 (ESP32-S3) — do not define M5STICK (StickC GPIO 25/26 RFID paths).
// Input is 2-button (HAS_BTN in .ini): SEL + DW with double/long-press; no HAS_3_BUTTONS.

#define USB_VID 0x1209
#define USB_PID 0x0001
#define USB_MANUFACTURER "Generic"
#define USB_PRODUCT "HID Keyboard"
#define USB_SERIAL "1"

static const uint8_t TX = 43;
static const uint8_t RX = 44;

static const uint8_t TXD2 = 9;
static const uint8_t RXD2 = 10;

// Default Arduino Wire aliases → Grove PORT
static const uint8_t SDA = 9;
static const uint8_t SCL = 10;

// Hat / shared SPI bus (SD, CC1101, NRF24, …)
static const uint8_t SS = 43;
static const uint8_t MOSI = 6;
static const uint8_t MISO = 4;
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
static const uint8_t G14 = 14;
static const uint8_t G15 = 15;
static const uint8_t G16 = 16;
static const uint8_t G17 = 17;
static const uint8_t G18 = 18;
static const uint8_t G21 = 21;
static const uint8_t G38 = 38;
static const uint8_t G39 = 39;
static const uint8_t G40 = 40;
static const uint8_t G41 = 41;
static const uint8_t G42 = 42;
static const uint8_t G43 = 43;
static const uint8_t G44 = 44;
static const uint8_t G45 = 45;
static const uint8_t G46 = 46;
static const uint8_t G47 = 47;
static const uint8_t G48 = 48;

static const uint8_t ADC1 = 1;
static const uint8_t ADC2 = 2;

// Deepsleep — front button (SEL)
#define DEEPSLEEP_WAKEUP_PIN 11
#define DEEPSLEEP_PIN_ACT LOW

#endif /* Pins_Arduino_h */
