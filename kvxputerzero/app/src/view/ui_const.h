/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace view {

// Phosphor Fill 2.1 codepoints (bundled Phosphor-Fill.ttf). Verified via GSUB
// name ligatures in the font — do not assume older Phosphor private-use maps.

// some font constants
constexpr const char* ICON_SIGN_OUT           = "\uE42A";
constexpr const char* ICON_TEXT_BOLD          = "\uE5BE";
constexpr const char* ICON_MOON               = "\uE330";
constexpr const char* ICON_SUN                = "\uE474";
constexpr const char* ICON_SQUARE_ARROW_LEFT  = "\uE074";
constexpr const char* ICON_SQUARE_ARROW_RIGHT = "\uE076";
constexpr const char* ICON_MINUS              = "\uE32A";
constexpr const char* ICON_PLUS               = "\uE3D4";
constexpr const char* ICON_INFO               = "\uE2CE";
constexpr const char* ICON_ARROW_UP           = "\uE08E";
constexpr const char* ICON_ARROW_DOWN         = "\uE03E";
constexpr const char* ICON_ARROW_LEFT         = "\uE058";
constexpr const char* ICON_ARROW_RIGHT        = "\uE06C";
constexpr const char* ICON_ARROW_BACK         = "\uE08A";
constexpr const char* ICON_CARET_UP           = "\uE13C";
constexpr const char* ICON_CARET_DOWN         = "\uE136";
constexpr const char* ICON_CARET_LEFT         = "\uE138";
constexpr const char* ICON_PLAY               = "\uE3D0";
constexpr const char* ICON_CHECK              = "\uE182";
constexpr const char* ICON_SKIP_BACK          = "\uE5A4";
constexpr const char* ICON_SKIP_FORWARD       = "\uE5A6";
constexpr const char* ICON_STOP               = "\uE46C";
constexpr const char* ICON_HOUSE              = "\uE2C2";
constexpr const char* ICON_WIFI_NONE          = "\uE4F0";
constexpr const char* ICON_WIFI_LOW           = "\uE4EC";
constexpr const char* ICON_WIFI_MEDIUM        = "\uE4EE";
constexpr const char* ICON_WIFI_HIGH          = "\uE4EA";
constexpr const char* ICON_ETHERNET           = "\uEDDE";
constexpr const char* ICON_BAT_FULL           = "\uE7C4";
constexpr const char* ICON_BAT_HIGH           = "\uE7C2";
constexpr const char* ICON_BAT_MEDIUM         = "\uE7C0";
constexpr const char* ICON_BAT_LOW            = "\uE7BE";
constexpr const char* ICON_BAT_EMPTY          = "\uE7C6";
constexpr const char* ICON_BAT_CHARGING       = "\uE0BC";
constexpr const char* ICON_DOT                = "\uE18A"; // circle-fill
constexpr const char* ICON_BADGE_SOON         = "\uE2BA"; // hourglass-simple-fill
constexpr const char* ICON_BADGE_STOCK        = "\uE5DE"; // arrow-square-out-fill

// Channel tiles (Phosphor Fill 2.1)
constexpr const char* ICON_CH_WIFI      = "\uE4EA"; // wifi-high
constexpr const char* ICON_CH_DISCOVERY = "\uEA64"; // binoculars
constexpr const char* ICON_CH_NETOPS    = "\uE408"; // share-network
constexpr const char* ICON_CH_BLE       = "\uE0DA"; // bluetooth
constexpr const char* ICON_CH_RF        = "\uEA9A"; // wave-sine
constexpr const char* ICON_CH_NRF24     = "\uE0F2"; // broadcast
constexpr const char* ICON_CH_LORA      = "\uEBAA"; // cell-tower
constexpr const char* ICON_CH_FM        = "\uE77E"; // radio
constexpr const char* ICON_CH_IR        = "\uEAE6"; // television-simple
constexpr const char* ICON_CH_ETH       = "\uEDDE"; // network
constexpr const char* ICON_CH_USB       = "\uE956"; // usb
constexpr const char* ICON_CH_GPS       = "\uEDD6"; // gps-fix
constexpr const char* ICON_CH_RFID      = "\uE2C8"; // identification-card
constexpr const char* ICON_CH_NFC       = "\uED42"; // contactless-payment
constexpr const char* ICON_CH_FILES     = "\uE256"; // folder-open
constexpr const char* ICON_CH_SCRIPTS   = "\uEAE8"; // terminal-window
constexpr const char* ICON_CH_CLOCK     = "\uE19A"; // clock
constexpr const char* ICON_CH_CHARGE    = "\uE0BC"; // battery-charging
constexpr const char* ICON_CH_TOOLS     = "\uECA0"; // toolbox
constexpr const char* ICON_CH_MODULES   = "\uE596"; // puzzle-piece
constexpr const char* ICON_CH_CONFIG    = "\uE272"; // gear-six

// PDA hub tiles
constexpr const char* ICON_PDA_NOTES      = "\uE34C"; // note-pencil
constexpr const char* ICON_PDA_MEMOS      = "\uE63E"; // notepad
constexpr const char* ICON_PDA_TODO       = "\uEADC"; // list-checks
constexpr const char* ICON_PDA_CALENDAR   = "\uE7B4"; // calendar-dots
constexpr const char* ICON_PDA_CONTACTS   = "\uE6F8"; // address-book
constexpr const char* ICON_PDA_ALARMS     = "\uE006"; // alarm
constexpr const char* ICON_PDA_WORLD      = "\uE28C"; // globe-hemisphere-west
constexpr const char* ICON_PDA_CALC       = "\uE538"; // calculator

// Submenu leading icons
constexpr const char* ICON_MENU_CODEC     = "\uE2A2"; // hash
constexpr const char* ICON_MENU_CALCS     = "\uE31E"; // math-operations
constexpr const char* ICON_MENU_PASSGEN   = "\uE752"; // password
constexpr const char* ICON_MENU_TOTP      = "\uE40C"; // shield-check
constexpr const char* ICON_MENU_COMBO     = "\uE2FE"; // lock-key
constexpr const char* ICON_MENU_QR        = "\uE3E6"; // qr-code
constexpr const char* ICON_MENU_BARCODE   = "\uE0B8"; // barcode
constexpr const char* ICON_MENU_PDA       = "\uE6F8"; // address-book
constexpr const char* ICON_MENU_TIMER     = "\uE492"; // timer
constexpr const char* ICON_MENU_MAIN_CLR  = "\uE6F0"; // paint-brush
constexpr const char* ICON_MENU_ACCENT    = "\uE210"; // drop
constexpr const char* ICON_MENU_BG        = "\uE392"; // paint-bucket
constexpr const char* ICON_MENU_RESET     = "\uE038"; // arrow-counter-clockwise
constexpr const char* ICON_MENU_ABOUT     = "\uE2CE"; // info
constexpr const char* ICON_MENU_CUSTOM    = "\uE568"; // eyedropper

namespace color {

// Naive UI common color tokens, flattened to solid RGB values for LVGL.
namespace light {
constexpr uint32_t kPrimary      = 0x18a058;
constexpr uint32_t kInfo         = 0x2080f0;
constexpr uint32_t kSuccess      = 0x18a058;
constexpr uint32_t kWarning      = 0xf0a020;
constexpr uint32_t kError        = 0xd03050;
constexpr uint32_t kBody         = 0xffffff;
constexpr uint32_t kCard         = 0xffffff;
constexpr uint32_t kAction       = 0xfafafc;
constexpr uint32_t kButton       = 0xf5f5f5;
constexpr uint32_t kBorder       = 0xe0e0e6;
constexpr uint32_t kTextPrimary  = 0x1f2225;
constexpr uint32_t kTextDisabled = 0xc2c2c2;
} // namespace light

namespace dark {
constexpr uint32_t kPrimary      = 0x63e2b7;
constexpr uint32_t kInfo         = 0x70c0e8;
constexpr uint32_t kSuccess      = 0x63e2b7;
constexpr uint32_t kWarning      = 0xf2c97d;
constexpr uint32_t kError        = 0xe88080;
constexpr uint32_t kBody         = 0x101014;
constexpr uint32_t kCard         = 0x18181c;
constexpr uint32_t kAction       = 0x0f0f0f;
constexpr uint32_t kButton       = 0x141414;
constexpr uint32_t kBorder       = 0x3d3d3d;
constexpr uint32_t kTextPrimary  = 0xe6e6e6;
constexpr uint32_t kTextDisabled = 0x616161;
} // namespace dark

} // namespace color

} // namespace view
