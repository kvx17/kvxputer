#include "scanner_list.h"
#include "kvx_ui.h"
#include "root/hal/radio_mem.h"
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <atomic>
#include <cstring>
#include <globals.h>

static constexpr int SCANNER_FOOTER_H = 14;
static constexpr unsigned long SCANNER_BAR_MS = 30000;

static int scannerBodyTop() { return KVX_TOPBAR_H + 2; }

static int scannerBodyBottom() { return tftHeight - SCANNER_FOOTER_H; }

static int scannerLineH() { return uiRowH(uiMenuFont()); }

static int scannerVisibleRows() {
    int h = scannerBodyBottom() - scannerBodyTop();
    return max(1, h / scannerLineH());
}

static void scannerDrawFooter() {
    const int dense = uiDenseFont();
    const uint16_t bg = kvxConfig.bgColor;
    const uint16_t fg = kvxConfig.priColor;
    tft.fillRect(0, scannerBodyBottom(), tftWidth, SCANNER_FOOTER_H, bg);
    tft.drawFastHLine(0, scannerBodyBottom(), tftWidth, kvxConfig.priColor);
    tft.setTextSize(dense);
    tft.setTextColor(fg, bg);
    tft.drawCentreString("[Up] Up  [OK] Select  [Esc] Back  [Down] Down", tftWidth / 2, scannerBodyBottom() + 2, 1);
}

static void scannerDrawDetailFooter() {
    const int dense = uiDenseFont();
    const uint16_t bg = kvxConfig.bgColor;
    const uint16_t fg = kvxConfig.priColor;
    tft.fillRect(0, scannerBodyBottom(), tftWidth, SCANNER_FOOTER_H, bg);
    tft.drawFastHLine(0, scannerBodyBottom(), tftWidth, kvxConfig.priColor);
    tft.setTextSize(dense);
    tft.setTextColor(fg, bg);
    tft.drawCentreString("[Esc] Back", tftWidth / 2, scannerBodyBottom() + 2, 1);
}

static void scannerDrawChrome(ScannerListState &st, bool forceBar) {
    const uint16_t bg = kvxConfig.bgColor;
    if (!st.chromeReady || forceBar) {
        tft.fillScreen(bg);
        if (st.status.length()) drawKvxTopBar(st.title.c_str(), st.status.c_str());
        else drawKvxTopBar(st.title.c_str());
        scannerDrawFooter();
        st.chromeReady = true;
        st.lastBarMs = millis();
        st.lastCursor = -1;
        st.lastScroll = -1;
        st.lastRowCount = -1;
        st.lastVisible.clear();
    } else if (millis() - st.lastBarMs > SCANNER_BAR_MS) {
        if (st.status.length()) drawKvxTopBar(st.title.c_str(), st.status.c_str());
        else drawKvxTopBar(st.title.c_str());
        st.lastBarMs = millis();
    }
}

static String scannerRowText(const ScannerListState &st, int idx, int nchars) {
    String line = "[" + String(idx + 1) + "] ";
    if (idx >= 0 && idx < (int)st.rows.size()) line += st.rows[idx];
    if ((int)line.length() > nchars) line = line.substring(0, nchars);
    return line;
}

static void scannerDrawRow(ScannerListState &st, int idx, int scroll, int nchars) {
    const int lineH = scannerLineH();
    const int startY = scannerBodyTop();
    const int y = startY + (idx - scroll) * lineH;
    if (y < startY || y + lineH > scannerBodyBottom()) return;

    const bool sel = (idx == st.cursor);
    const uint16_t bg = sel ? kvxConfig.secColor : kvxConfig.bgColor;
    const uint16_t fg = sel ? kvxConfig.bgColor : kvxConfig.priColor;

    tft.fillRect(0, y, tftWidth, lineH, bg);
    tft.setTextSize(uiMenuFont());
    tft.setTextColor(fg, bg);
    tft.drawString(scannerRowText(st, idx, nchars), 4, y + 1, 1);
}

static void scannerRedrawBody(ScannerListState &st, bool forceAll) {
    const int visible = scannerVisibleRows();
    const int lineH = scannerLineH();
    const int startY = scannerBodyTop();
    const int nchars = max(1, (tftWidth - 8) / uiCharW(uiMenuFont()));
    const int count = (int)st.rows.size();

    if (st.cursor < 0) st.cursor = 0;
    if (count == 0) {
        st.cursor = 0;
        st.scroll = 0;
    } else if (st.cursor >= count) {
        st.cursor = count - 1;
    }
    if (st.cursor < st.scroll) st.scroll = st.cursor;
    if (st.cursor >= st.scroll + visible) st.scroll = st.cursor - visible + 1;
    if (st.scroll < 0) st.scroll = 0;

    const bool listChanged = forceAll || st.lastRowCount != count || st.lastScroll != st.scroll ||
                             (int)st.lastVisible.size() != min(visible, max(0, count - st.scroll));

    if (listChanged) {
        tft.fillRect(0, startY, tftWidth, scannerBodyBottom() - startY, kvxConfig.bgColor);
        st.lastVisible.clear();
        for (int i = 0; i < visible; i++) {
            int idx = st.scroll + i;
            if (idx >= count) break;
            scannerDrawRow(st, idx, st.scroll, nchars);
            st.lastVisible.push_back(st.rows[idx]);
        }
        if (count == 0) {
            tft.setTextSize(uiDenseFont());
            tft.setTextColor(TFT_DARKGREY, kvxConfig.bgColor);
            const char *placeholder = st.status.length() ? st.status.c_str() : "Scanning...";
            tft.drawString(placeholder, 8, startY + 4, 1);
        }
    } else {
        // Patch only changed visible labels or selection move.
        for (int i = 0; i < visible; i++) {
            int idx = st.scroll + i;
            if (idx >= count) break;
            bool labelChanged = (i >= (int)st.lastVisible.size()) || st.lastVisible[i] != st.rows[idx];
            bool selChanged = (idx == st.cursor || idx == st.lastCursor) && st.cursor != st.lastCursor;
            if (labelChanged || selChanged || forceAll) {
                scannerDrawRow(st, idx, st.scroll, nchars);
                if (i < (int)st.lastVisible.size()) st.lastVisible[i] = st.rows[idx];
                else st.lastVisible.push_back(st.rows[idx]);
            }
        }
    }

    st.lastCursor = st.cursor;
    st.lastScroll = st.scroll;
    st.lastRowCount = count;
}

void scannerListBegin(ScannerListState &st, const char *title, const char *status) {
    // Direct-to-panel like Charge: nested top-bar frames were presenting an empty
    // canvas over the body, so rows never appeared after "Scanning...".
    tftAbortFrame();
    tftSuppressCanvas(true);
    returnToMenu = false;
    st = ScannerListState();
    st.title = title ? title : "Scan";
    if (status) st.status = status;
    EscPress = false;
    SelPress = false;
    UpPress = false;
    DownPress = false;
    PrevPress = false;
    NextPress = false;
    AnyKeyPress = false;
    scannerDrawChrome(st, true);
    scannerRedrawBody(st, true);
}

void scannerListEnd() {
    tftSuppressCanvas(false);
    tftAbortFrame();
}

void scannerListRefresh(ScannerListState &st) {
    tftAbortFrame();
    tftSuppressCanvas(true);
    scannerDrawChrome(st, true);
    scannerRedrawBody(st, true);
}

void scannerListSetStatus(ScannerListState &st, const char *status) {
    String next = status ? String(status) : String();
    if (next == st.status) return;
    st.status = next;
    unsigned long now = millis();
    if (st.chromeReady && now - st.lastBarMs < 220) return;
    if (st.chromeReady) {
        if (st.status.length()) drawKvxTopBar(st.title.c_str(), st.status.c_str());
        else drawKvxTopBar(st.title.c_str());
        st.lastBarMs = now;
        if (st.rows.empty()) scannerRedrawBody(st, true);
    }
}

void scannerListSetRows(ScannerListState &st, const std::vector<String> &rows) {
    const bool countChanged = (int)rows.size() != (int)st.rows.size();
    st.rows = rows;
    unsigned long now = millis();
    if (!countChanged && now - st.lastPaintMs < 220) return;
    st.lastPaintMs = now;
    scannerDrawChrome(st, false);
    scannerRedrawBody(st, countChanged || st.lastRowCount <= 0);
}

ScannerListResult scannerListPoll(ScannerListState &st) {
    // Ignore leftover returnToMenu from the parent loopOptions — Esc/forceHome only.
    if (forceHome) return SCANNER_LIST_EXIT;
    if (check(EscPress)) return SCANNER_LIST_EXIT;

    bool moved = false;
    if (check(UpPress) || check(PrevPress)) {
        if (st.cursor > 0) {
            st.cursor--;
            moved = true;
        }
    }
    if (check(DownPress) || check(NextPress)) {
        if (st.cursor + 1 < (int)st.rows.size()) {
            st.cursor++;
            moved = true;
        }
    }
    if (moved) scannerRedrawBody(st, false);

    if (check(SelPress)) {
        if (!st.rows.empty() && st.cursor >= 0 && st.cursor < (int)st.rows.size()) {
            return SCANNER_LIST_DETAIL;
        }
    }

    // Keep top bar alive without full redraw.
    scannerDrawChrome(st, false);
    return SCANNER_LIST_RUNNING;
}

void scannerListShowDetail(
    const char *title, const std::vector<ScannerDetailField> &fields, const std::function<void()> &tick
) {
    const uint16_t bg = kvxConfig.bgColor;
    const uint16_t fg = kvxConfig.priColor;
    const uint16_t accent = kvxConfig.secColor;
    const int bodyFont = uiBodyFont();
    const int lineH = uiRowH(bodyFont);
    const int startY = KVX_TOPBAR_H + 4;
    const int footerY = scannerBodyBottom();
    const int visible = max(1, (footerY - startY) / lineH);
    const int maxChars = max(1, (tftWidth - 8) / uiCharW(bodyFont));
    int scroll = 0;

    // Expand fields into paint rows: label (accent) + value; long values on their own line.
    struct PaintRow {
        String text;
        bool isLabel;
    };
    std::vector<PaintRow> rows;
    auto pushWrapped = [&](const String &text, bool isLabel) {
        String rest = text;
        while (rest.length() > 0) {
            if ((int)rest.length() <= maxChars) {
                rows.push_back({rest, isLabel});
                break;
            }
            rows.push_back({rest.substring(0, maxChars), isLabel});
            rest = rest.substring(maxChars);
            isLabel = false; // continuations use value color
        }
    };

    for (const auto &f : fields) {
        String lab = f.label;
        String val = f.value;
        // BSSID / Beacon int: label on one line, value on the next (full width, wrap if needed)
        bool valueBelow = lab.equalsIgnoreCase("BSSID") || lab.equalsIgnoreCase("Beacon int") ||
                          lab.equalsIgnoreCase("Revealed");
        if (valueBelow) {
            pushWrapped(lab, true);
            pushWrapped(val, false);
        } else {
            String combined = lab + ": " + val;
            if ((int)combined.length() <= maxChars) {
                // Draw as two colors in paint — store "LAB\x01VAL" marker
                rows.push_back({lab + "\x01" + val, true});
            } else {
                pushWrapped(lab, true);
                pushWrapped(val, false);
            }
        }
    }

    auto paint = [&]() {
        TftFrame frame;
        tft.fillScreen(bg);
        drawKvxTopBar(title && title[0] ? title : "RESULT DETAILS");
        scannerDrawDetailFooter();
        tft.setTextSize(bodyFont);
        for (int i = 0; i < visible; i++) {
            int idx = scroll + i;
            if (idx >= (int)rows.size()) break;
            int y = startY + i * lineH;
            tft.fillRect(0, y, tftWidth, lineH, bg);
            const PaintRow &pr = rows[idx];
            int sep = pr.text.indexOf('\x01');
            if (sep >= 0) {
                String lab = pr.text.substring(0, sep);
                String val = pr.text.substring(sep + 1);
                tft.setTextColor(accent, bg);
                tft.drawString(lab + ":", 4, y + 1, 1);
                int lx = 4 + (int)(lab.length() + 1) * uiCharW(bodyFont);
                tft.setTextColor(fg, bg);
                tft.drawString(val, lx + uiCharW(bodyFont), y + 1, 1);
            } else if (pr.isLabel) {
                tft.setTextColor(accent, bg);
                tft.drawString(pr.text, 4, y + 1, 1);
            } else {
                tft.setTextColor(fg, bg);
                tft.drawString(pr.text, 4, y + 1, 1);
            }
        }
    };

    EscPress = false;
    SelPress = false;
    returnToMenu = false;
    paint();

    while (!forceHome) {
        if (tick) tick();
        if (check(EscPress)) break;
        if (check(SelPress)) break; // OK also backs out of detail
        bool redraw = false;
        if (check(UpPress) || check(PrevPress)) {
            if (scroll > 0) {
                scroll--;
                redraw = true;
            }
        }
        if (check(DownPress) || check(NextPress)) {
            if (scroll + visible < (int)rows.size()) {
                scroll++;
                redraw = true;
            }
        }
        if (redraw) paint();
        delay(20);
    }
    delay(80);
    while (check(EscPress) || check(SelPress) || check(AnyKeyPress)) delay(10);
}

namespace {

struct ScannerAdvRaw {
    char mac[18];
    uint8_t payload[62];
    uint8_t payloadLen;
    int8_t rssi;
    uint8_t addrType;
};

constexpr size_t kAdvRing = 48;
ScannerAdvRaw g_advRing[kAdvRing];
volatile uint16_t g_advHead = 0;
volatile uint16_t g_advTail = 0;
portMUX_TYPE g_advMux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint32_t> g_advCount{0};

void captureAdv(const NimBLEAdvertisedDevice *d) {
    if (!d) return;
    ScannerAdvRaw raw;
    memset(&raw, 0, sizeof(raw));
    const uint8_t *a = d->getAddress().getVal();
    snprintf(
        raw.mac,
        sizeof(raw.mac),
        "%02X:%02X:%02X:%02X:%02X:%02X",
        a[5],
        a[4],
        a[3],
        a[2],
        a[1],
        a[0]
    );
    raw.rssi = d->getRSSI();
    raw.addrType = d->getAddressType();
    const std::vector<uint8_t> &pl = d->getPayload();
    raw.payloadLen = (uint8_t)min(pl.size(), sizeof(raw.payload));
    if (raw.payloadLen) memcpy(raw.payload, pl.data(), raw.payloadLen);

    g_advCount.fetch_add(1, std::memory_order_relaxed);
    portENTER_CRITICAL(&g_advMux);
    uint16_t next = (uint16_t)((g_advHead + 1) % kAdvRing);
    if (next == g_advTail) g_advTail = (uint16_t)((g_advTail + 1) % kAdvRing);
    g_advRing[g_advHead] = raw;
    g_advHead = next;
    portEXIT_CRITICAL(&g_advMux);
}

void parsePayloadInto(ScannerAdvSnap &s) {
    const uint8_t *p = s.payload.data();
    const size_t len = s.payload.size();
    size_t i = 0;
    while (i + 1 < len) {
        uint8_t dlen = p[i];
        if (dlen == 0) break;
        if (i + 1 + dlen > len) break;
        uint8_t type = p[i + 1];
        const uint8_t *v = p + i + 2;
        uint8_t vlen = (uint8_t)(dlen - 1);
        if ((type == 0x08 || type == 0x09) && vlen && !s.name.length()) {
            s.name = String((const char *)v, vlen);
            s.name.trim();
        } else if (type == 0xFF && vlen) {
            s.haveMfg = true;
            s.mfg.assign((const char *)v, vlen);
        } else if ((type == 0x02 || type == 0x03) && vlen >= 2 && !s.haveServiceUUID) {
            uint16_t u = v[0] | ((uint16_t)v[1] << 8);
            char hex[5];
            snprintf(hex, sizeof(hex), "%04X", u);
            s.serviceUUID = hex;
            s.haveServiceUUID = true;
        } else if (type == 0x0A && vlen >= 1) {
            s.haveTXPower = true;
            s.txPower = (int8_t)v[0];
        } else if (type == 0x19 && vlen >= 2) {
            s.haveAppearance = true;
            s.appearance = v[0] | ((uint16_t)v[1] << 8);
        }
        i += (size_t)dlen + 1;
    }
}

ScannerAdvSnap snapFromRaw(const ScannerAdvRaw &raw) {
    ScannerAdvSnap s;
    s.mac = raw.mac;
    s.rssi = raw.rssi;
    s.addrType = raw.addrType;
    if (raw.payloadLen) s.payload.assign(raw.payload, raw.payload + raw.payloadLen);
    parsePayloadInto(s);
    return s;
}

class ScannerAdvCallbacks : public NimBLEScanCallbacks {
    void onDiscovered(const NimBLEAdvertisedDevice *d) override { captureAdv(d); }
    void onResult(const NimBLEAdvertisedDevice *d) override { captureAdv(d); }
} g_scannerCbs;

} // namespace

NimBLEScan *scannerBleStart() {
    portENTER_CRITICAL(&g_advMux);
    g_advHead = 0;
    g_advTail = 0;
    portEXIT_CRITICAL(&g_advMux);
    g_advCount.store(0, std::memory_order_relaxed);

    if (WiFi.getMode() != WIFI_MODE_NULL || wifiConnected) {
        wifiDisconnect();
        delay(200);
    }

    uiRamEnterHeavy();

    if (NimBLEDevice::isInitialized()) {
        NimBLEScan *old = NimBLEDevice::getScan();
        if (old) {
            old->stop();
            delay(20);
        }
        NimBLEDevice::deinit(true);
        delay(80);
    }

    NimBLEDevice::init("");
    delay(150);

    NimBLEScan *scan = NimBLEDevice::getScan();
    if (!scan) {
        uiRamLeaveHeavy();
        return nullptr;
    }

    scan->setScanCallbacks(&g_scannerCbs, true);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
    scan->setDuplicateFilter(false);
    scan->setMaxResults(0);
    scan->setScanResponseTimeout(80);

    bool ok = false;
    for (int i = 0; i < 6 && !ok; i++) {
        ok = scan->start(0, false, true);
        if (!ok) delay(120);
    }
    if (!(ok || scan->isScanning())) {
        NimBLEDevice::deinit(true);
        uiRamLeaveHeavy();
        return nullptr;
    }
    return scan;
}

uint32_t scannerBleAdvCount() { return g_advCount.load(std::memory_order_relaxed); }

std::vector<ScannerAdvSnap> scannerBleTakeInbox(NimBLEScan *scan) {
    (void)scan;
    ScannerAdvRaw tmp[kAdvRing];
    size_t n = 0;
    portENTER_CRITICAL(&g_advMux);
    while (g_advTail != g_advHead && n < kAdvRing) {
        tmp[n++] = g_advRing[g_advTail];
        g_advTail = (uint16_t)((g_advTail + 1) % kAdvRing);
    }
    portEXIT_CRITICAL(&g_advMux);

    std::vector<ScannerAdvSnap> out;
    out.reserve(n);
    for (size_t i = 0; i < n; i++) out.push_back(snapFromRaw(tmp[i]));
    return out;
}

void scannerBleKeepAlive(NimBLEScan *scan) {
    if (!scan) return;
    if (!scan->isScanning()) scan->start(0, true, true);
}

void scannerBleTeardown(bool deinit) {
    portENTER_CRITICAL(&g_advMux);
    g_advHead = 0;
    g_advTail = 0;
    portEXIT_CRITICAL(&g_advMux);
    NimBLEScan *scan = nullptr;
    if (NimBLEDevice::isInitialized()) scan = NimBLEDevice::getScan();
    if (scan) {
        scan->stop();
        delay(20);
        scan->clearResults();
    }
    if (deinit && NimBLEDevice::isInitialized()) {
        delay(30);
        NimBLEDevice::deinit(true);
    }
    uiRamLeaveHeavy();
}
