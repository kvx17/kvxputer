#pragma once

#include <Arduino.h>
#include <functional>
#include <string>
#include <vector>

class NimBLEScan;

// Shared live-scan list + detail chrome for Wall of Flipper / AirTag / Skimmer / BLE Scan.
// Top bar = drawKvxTopBar(appName); selected row fills with accent (secColor); dense footer.

enum ScannerListResult {
    SCANNER_LIST_RUNNING = 0, // keep scanning / polling
    SCANNER_LIST_EXIT,        // Esc / Back — leave the app
    SCANNER_LIST_DETAIL,      // OK on a row — open detail for cursor index
};

struct ScannerDetailField {
    String label;
    String value;
};

struct ScannerListState {
    String title;
    String status; // optional mid-bar status (e.g. "scanning…")
    std::vector<String> rows;
    int cursor = 0;
    int scroll = 0;
    bool chromeReady = false;
    unsigned long lastBarMs = 0;
    // Dirty-draw cache
    int lastCursor = -1;
    int lastScroll = -1;
    int lastRowCount = -1;
    std::vector<String> lastVisible;
    unsigned long lastPaintMs = 0;
};

void scannerListBegin(ScannerListState &st, const char *title, const char *status = nullptr);
void scannerListEnd();
void scannerListRefresh(ScannerListState &st);
void scannerListSetStatus(ScannerListState &st, const char *status);
// Replace list rows; dirty-redraws only what changed. Clamps cursor/scroll.
void scannerListSetRows(ScannerListState &st, const std::vector<String> &rows);
// Poll Up/Down/OK/Esc. Call once per loop after updating rows.
ScannerListResult scannerListPoll(ScannerListState &st);
// Full-screen Label: value detail. Esc/Back returns. Optional tick while waiting (e.g. keep scan alive).
void scannerListShowDetail(
    const char *title, const std::vector<ScannerDetailField> &fields,
    const std::function<void()> &tick = nullptr
);

struct ScannerAdvSnap {
    String mac;
    String name;
    int rssi = 0;
    uint8_t addrType = 0;
    std::vector<uint8_t> payload;
    std::string mfg;
    bool haveMfg = false;
    bool haveServiceUUID = false;
    String serviceUUID;
    bool haveTXPower = false;
    int8_t txPower = 0;
    bool haveAppearance = false;
    uint16_t appearance = 0;
};

// Bring up NimBLE observer scan (drop Wi-Fi, wait for host, callbacks, retry start).
NimBLEScan *scannerBleStart();
uint32_t scannerBleAdvCount();
std::vector<ScannerAdvSnap> scannerBleTakeInbox(NimBLEScan *scan);
void scannerBleKeepAlive(NimBLEScan *scan);
// Safe NimBLE teardown used by live scanners (stop → clear → deinit).
void scannerBleTeardown(bool deinit = true);
