#include <Arduino.h>
#include "root/ui/quick_search.h"

#ifdef HAS_KEYBOARD

#include "root/app/app_catalog.h"
#include "root/app/powerSave.h"
#include "root/config/config.h"
#include "root/hal/led_control.h"
#include "root/input/mykeyboard.h"
#include "root/storage/file_search.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include "root/ui/quick_search_match.h"
#include <LittleFS.h>
#include <SD.h>
#include <algorithm>
#include <globals.h>

namespace {

enum class SearchMode : uint8_t { Apps = 0, Files = 1 };

struct SearchHit {
    String id;       // app catalog id, or "sd:path" / "lfs:path"
    String label;
    String subtitle;
    int score = 0;
};

static constexpr size_t kMaxRecents = 4;
static constexpr unsigned long kBackspaceClearMs = 400;
static constexpr unsigned long kFileScanBudgetMs = 24;
static constexpr unsigned long kNavRepeatMs = 140;

static int footerH() { return uiLineH(FP) + 4; }
static int inputH() { return uiLineH(FP) + 8; }

static void pushRecentApp(const String &id) {
    if (id.length() == 0) return;
    auto &v = kvxConfig.searchRecentApps;
    v.erase(std::remove(v.begin(), v.end(), id), v.end());
    v.insert(v.begin(), id);
    while (v.size() > kMaxRecents) v.pop_back();
    kvxConfig.saveFile();
}

static void pushRecentFile(const String &encoded) {
    if (encoded.length() == 0) return;
    auto &v = kvxConfig.searchRecentFiles;
    v.erase(std::remove(v.begin(), v.end(), encoded), v.end());
    v.insert(v.begin(), encoded);
    while (v.size() > kMaxRecents) v.pop_back();
    kvxConfig.saveFile();
}

static String encodeFilePath(bool onSd, const String &path) {
    return String(onSd ? "sd:" : "lfs:") + path;
}

static bool decodeFilePath(const String &encoded, bool &onSd, String &path) {
    if (encoded.startsWith("sd:")) {
        onSd = true;
        path = encoded.substring(3);
        return path.length() > 0;
    }
    if (encoded.startsWith("lfs:")) {
        onSd = false;
        path = encoded.substring(4);
        return path.length() > 0;
    }
    return false;
}

static void buildAppResults(const String &query, std::vector<SearchHit> &out) {
    out.clear();
    const String nq = qsNormalize(query);

    if (nq.length() == 0) {
        for (const String &id : kvxConfig.searchRecentApps) {
            if (out.size() >= kMaxRecents) break;
            bool ok = false;
            String label = id;
            String group;
            for (const auto &item : appCatalogItems()) {
                if (id != item.id) continue;
                if (!appCatalogAvailable(item)) break;
                label = item.label;
                group = item.group;
                ok = true;
                break;
            }
            if (!ok) continue;
            out.push_back({id, label, group.length() ? group : String("Recent"), 0});
        }
        return;
    }

    for (const auto &item : appCatalogItems()) {
        if (!appCatalogAvailable(item)) continue;
        const int score = qsScoreMatch(nq, item.label, item.id, item.group);
        if (score <= 0) continue;
        int boost = 0;
        for (size_t i = 0; i < kvxConfig.searchRecentApps.size(); i++) {
            if (kvxConfig.searchRecentApps[i] == item.id) {
                boost = (int)(kMaxRecents - i) * 5;
                break;
            }
        }
        out.push_back({String(item.id), String(item.label), String(item.group), score + boost});
    }

    std::sort(out.begin(), out.end(), [](const SearchHit &a, const SearchHit &b) {
        if (a.score != b.score) return a.score > b.score;
        return a.label.compareTo(b.label) < 0;
    });
}

static void buildFileRecentResults(std::vector<SearchHit> &out) {
    out.clear();
    for (const String &enc : kvxConfig.searchRecentFiles) {
        if (out.size() >= kMaxRecents) break;
        bool onSd = false;
        String path;
        if (!decodeFilePath(enc, onSd, path)) continue;
        FS *fs = onSd ? (FS *)&SD : (FS *)&LittleFS;
        if (onSd && !setupSdCard()) continue;
        if (!fs->exists(path)) continue;
        int slash = path.lastIndexOf('/');
        String base = (slash >= 0) ? path.substring(slash + 1) : path;
        String parent = (slash > 0) ? path.substring(0, slash) : String("/");
        if (parent.length() > 28) parent = String("…") + parent.substring(parent.length() - 26);
        out.push_back({enc, base, parent, 0});
    }
}

static void buildFileScanResults(const FileSearchScanner &scan, std::vector<SearchHit> &out) {
    out.clear();
    for (const auto &h : scan.hits()) {
        out.push_back({encodeFilePath(h.onSd, h.path), h.basename, h.parent, 1});
    }
}

static int bodyTop() { return inputH() + 2; }
static int bodyH() { return tftHeight - bodyTop() - footerH(); }
static int visibleRows() { return max(1, bodyH() / uiRowH(FP)); }

static void drawHitRow(const SearchHit &hit, int y, bool selected) {
    const int rowH = uiRowH(FP);
    const uint16_t bg = selected ? kvxConfig.secColor : kvxConfig.bgColor;
    tft.fillRect(0, y, tftWidth, rowH, bg);
    tft.setTextSize(FP);
    tft.setTextColor(kvxConfig.priColor, bg);
    String mark = selected ? "> " : "  ";
    String label = mark + hit.label;
    int labelMax = max(6, (tftWidth - 4) / max(1, uiCharW(FP)));
    if ((int)label.length() > labelMax) label = label.substring(0, labelMax - 1) + "…";
    tft.drawString(label, 2, y + 1);
    if (hit.subtitle.length()) {
        String sub = hit.subtitle;
        int subW = sub.length() * uiCharW(FP);
        int labelW = label.length() * uiCharW(FP);
        if (labelW + subW + 10 < tftWidth) { tft.drawRightString(sub, tftWidth - 2, y + 1, 1); }
    }
}

static void drawInputBar(SearchMode mode, const String &query) {
    const int ih = inputH();
    tft.fillRect(0, 0, tftWidth, ih, kvxConfig.secColor);
    tft.setTextSize(FP);
    tft.setTextColor(kvxConfig.priColor, kvxConfig.secColor);
    String modeTag = (mode == SearchMode::Apps) ? "App" : "File";
    String line = modeTag + ": " + query + "_";
    int maxChars = max(8, (tftWidth - 8) / max(1, uiCharW(FP)));
    if ((int)line.length() > maxChars) line = String("…") + line.substring(line.length() - (maxChars - 1));
    tft.drawString(line, 4, (ih - uiLineH(FP)) / 2);
}

static void drawFooterBar(const String &query, size_t hitCount, bool scanning) {
    const int fh = footerH();
    tft.fillRect(0, tftHeight - fh, tftWidth, fh, kvxConfig.secColor);
    tft.setTextSize(FP);
    tft.setTextColor(kvxConfig.priColor, kvxConfig.secColor);
    // ; , . / navigate; FN+those insert the symbols; FN+1-4 launch.
    String foot = "↑↓ FN+1-4 Ent Esc Tab";
    if (query.length() && !scanning) foot = String(hitCount) + "  " + foot;
    else if (scanning) foot = String("… ") + foot;
    if ((int)foot.length() * uiCharW(FP) > tftWidth - 4) foot = "↑↓ FN+1-4 Esc";
    tft.drawCentreString(foot, tftWidth / 2, tftHeight - fh + 2, 1);
}

static void drawBody(
    SearchMode mode, const String &query, const std::vector<SearchHit> &hits, int selected, int scroll,
    bool scanning
) {
    const int top = bodyTop();
    const int h = bodyH();
    const int rowH = uiRowH(FP);
    const int visible = visibleRows();

    tft.fillRect(0, top, tftWidth, h, kvxConfig.bgColor);

    if (hits.empty()) {
        tft.setTextSize(FP);
        tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
        const char *msg = scanning                ? "Searching…"
                          : (query.length() == 0) ? (mode == SearchMode::Apps ? "Type to search apps"
                                                                              : "Type to search files")
                                                  : "No matches";
        tft.drawCentreString(msg, tftWidth / 2, top + h / 2 - uiLineH(FP) / 2, 1);
        return;
    }

    for (int i = 0; i < visible; i++) {
        int idx = scroll + i;
        if (idx < 0 || idx >= (int)hits.size()) break;
        drawHitRow(hits[idx], top + i * rowH, idx == selected);
    }
}

static void drawFull(
    SearchMode mode, const String &query, const std::vector<SearchHit> &hits, int selected, int scroll,
    bool scanning
) {
    tft.fillScreen(kvxConfig.bgColor);
    drawInputBar(mode, query);
    drawBody(mode, query, hits, selected, scroll, scanning);
    drawFooterBar(query, hits.size(), scanning);
}

// Selection moved within the same scroll window: paint only the two rows.
static void drawSelectionChange(
    const std::vector<SearchHit> &hits, int oldSelected, int newSelected, int scroll
) {
    if (hits.empty()) return;
    const int top = bodyTop();
    const int rowH = uiRowH(FP);
    const int visible = visibleRows();

    auto paintIfVisible = [&](int idx, bool sel) {
        if (idx < scroll || idx >= scroll + visible) return;
        if (idx < 0 || idx >= (int)hits.size()) return;
        drawHitRow(hits[idx], top + (idx - scroll) * rowH, sel);
    };

    if (oldSelected != newSelected) paintIfVisible(oldSelected, false);
    paintIfVisible(newSelected, true);
}

static void ensureSelectionVisible(int selected, int &scroll, int visible, int count) {
    if (count <= 0) {
        scroll = 0;
        return;
    }
    if (selected < scroll) scroll = selected;
    if (selected >= scroll + visible) scroll = selected - visible + 1;
    if (scroll < 0) scroll = 0;
    int maxScroll = max(0, count - visible);
    if (scroll > maxScroll) scroll = maxScroll;
}

static bool launchAppHit(const SearchHit &hit) {
    ledSetStatus(LED_STATUS_BUSY);
    const bool ok = appCatalogLaunch(hit.id);
    ledSetStatus(LED_STATUS_IDLE);
    if (ok) pushRecentApp(hit.id);
    else displayError("Unavailable", true);
    return ok;
}

static bool openFileHit(const SearchHit &hit) {
    bool onSd = false;
    String path;
    if (!decodeFilePath(hit.id, onSd, path)) {
        displayError("Bad path", true);
        return false;
    }
    if (onSd && !setupSdCard()) {
        displayError("No SD card", true);
        return false;
    }
    FS &fs = onSd ? (FS &)SD : (FS &)LittleFS;
    if (!fs.exists(path)) {
        displayError("Missing file", true);
        return false;
    }

    File f = fs.open(path);
    const bool isDir = f && f.isDirectory();
    if (f) f.close();

    ledSetStatus(LED_STATUS_BUSY);
    if (isDir) {
        loopSD(fs, false, "*", path);
    } else {
        // Prefer viewer for any file; Files menu uses the same path for text/images.
        viewFile(fs, path);
    }
    ledSetStatus(LED_STATUS_IDLE);
    pushRecentFile(hit.id);
    return true;
}

static void clearNavPulses() {
    UpPress = false;
    DownPress = false;
    PrevPress = false;
    NextPress = false;
    NextPagePress = false;
    PrevPagePress = false;
    SelPress = false;
    EscPress = false;
}

} // namespace

void quickSearchLoop() {
    SearchMode mode = SearchMode::Apps;
    String query;
    std::vector<SearchHit> hits;
    int selected = 0;
    int scroll = 0;
    int drawnSelected = -1;
    int drawnScroll = -1;
    bool fullRedraw = true;
    bool listDirty = true;
    bool bodyDirty = false;
    bool inputDirty = false;
    bool footerDirty = false;

    FileSearchScanner fileScan;
    String lastFileQuery;
    bool lastScanning = false;

    unsigned long backspaceHeldSince = 0;
    bool backspaceLatched = false;
    bool fnDigitLatched = false;
    unsigned long lastNavMs = 0;

    clearNavPulses();
    KeyStroke.Clear();
    resetHeldNavKeys();

    buildAppResults(query, hits);
    listDirty = false;

    while (!returnToMenu && !forceHome) {
        checkPowerSaveTime();

        if (listDirty) {
            if (mode == SearchMode::Apps) {
                fileScan.clear();
                lastFileQuery = "";
                buildAppResults(query, hits);
            } else if (query.length() == 0) {
                fileScan.clear();
                lastFileQuery = "";
                buildFileRecentResults(hits);
            } else {
                lastFileQuery = query;
                fileScan.restart(query);
                buildFileScanResults(fileScan, hits);
            }
            selected = 0;
            scroll = 0;
            listDirty = false;
            fullRedraw = true;
        }

        if (mode == SearchMode::Files && query.length() > 0 && !fileScan.done()) {
            const size_t before = fileScan.hits().size();
            fileScan.tick(kFileScanBudgetMs);
            if (fileScan.hits().size() != before || fileScan.done()) {
                buildFileScanResults(fileScan, hits);
                if (selected >= (int)hits.size()) selected = max(0, (int)hits.size() - 1);
                bodyDirty = true;
                footerDirty = true;
            }
        }

        const int visible = visibleRows();
        ensureSelectionVisible(selected, scroll, visible, (int)hits.size());

        const bool scanning =
            (mode == SearchMode::Files && query.length() > 0 && !fileScan.done());
        if (scanning != lastScanning) {
            footerDirty = true;
            lastScanning = scanning;
        }

        if (fullRedraw) {
            drawFull(mode, query, hits, selected, scroll, scanning);
            drawnSelected = selected;
            drawnScroll = scroll;
            fullRedraw = false;
            bodyDirty = false;
            inputDirty = false;
            footerDirty = false;
        } else {
            if (inputDirty) {
                drawInputBar(mode, query);
                inputDirty = false;
            }

            if (bodyDirty || scroll != drawnScroll) {
                // List content or scroll window changed — repaint body only.
                drawBody(mode, query, hits, selected, scroll, scanning);
                drawnSelected = selected;
                drawnScroll = scroll;
                bodyDirty = false;
            } else if (selected != drawnSelected) {
                // Same scroll window: flip only the old/new highlight rows.
                drawSelectionChange(hits, drawnSelected, selected, scroll);
                drawnSelected = selected;
            }

            if (footerDirty) {
                drawFooterBar(query, hits.size(), scanning);
                footerDirty = false;
            }
        }

        // Long-press Backspace clears query.
        if (KeyStroke.del || KeyStroke.pressed) {
            bool delHeld = KeyStroke.del;
            if (delHeld) {
                if (backspaceHeldSince == 0) backspaceHeldSince = millis();
                if (!backspaceLatched && millis() - backspaceHeldSince >= kBackspaceClearMs) {
                    if (query.length()) {
                        query = "";
                        listDirty = true;
                    }
                    backspaceLatched = true;
                    KeyStroke.Clear();
                }
            } else {
                backspaceHeldSince = 0;
                backspaceLatched = false;
            }
        } else {
            backspaceHeldSince = 0;
            if (!KeyStroke.del) backspaceLatched = false;
        }

        if (check(EscPress)) {
            clearNavPulses();
            KeyStroke.Clear();
            return;
        }

        if (check(SelPress) || (KeyStroke.pressed && KeyStroke.enter && !KeyStroke.fn)) {
            KeyStroke.Clear();
            clearNavPulses();
            if (!hits.empty() && selected >= 0 && selected < (int)hits.size()) {
                const SearchHit hit = hits[selected];
                bool ok = (mode == SearchMode::Apps) ? launchAppHit(hit) : openFileHit(hit);
                if (ok) return; // back to main grid after launch
                fullRedraw = true;
            }
            continue;
        }

        if (!KeyStroke.pressed) {
            fnDigitLatched = false;
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }

        keyStroke key = KeyStroke;
        KeyStroke.Clear();
        // Swallow arrow nav pulses — FN codes drive list movement.
        clearNavPulses();

        const bool fn = key.fn || isFnKeyHeld();

        // Tab toggles Apps / Files (backtick is Esc on Cardputer — do not use it here).
        bool toggled = false;
        for (char c : key.word) {
            const uint8_t u = (uint8_t)c;
            if (u == 0xB3 || u == '\t') {
                mode = (mode == SearchMode::Apps) ? SearchMode::Files : SearchMode::Apps;
                query = "";
                listDirty = true;
                fileScan.clear();
                lastFileQuery = "";
                toggled = true;
                break;
            }
        }
        if (toggled) continue;

        // Navigation without FN: ; up, . down, , left/page, / right/page.
        // With FN those same keys insert the symbols (Cardputer reports 0xDA.. as FN+arrows).
        {
            const unsigned long now = millis();
            const bool navOk = (now - lastNavMs) >= kNavRepeatMs;
            bool handled = false;

            if (fn) {
                for (char c : key.word) {
                    const uint8_t u = (uint8_t)c;
                    char insert = 0;
                    if (u == 0xDA) insert = ';';      // FN + ;
                    else if (u == 0xD9) insert = '.'; // FN + .
                    else if (u == 0xD8) insert = ','; // FN + ,
                    else if (u == 0xD7) insert = '/'; // FN + /
                    else if (c >= '1' && c <= '4' && !fnDigitLatched) {
                        int rank = c - '1';
                        if (rank < (int)hits.size()) {
                            fnDigitLatched = true;
                            const SearchHit hit = hits[rank];
                            bool ok = (mode == SearchMode::Apps) ? launchAppHit(hit) : openFileHit(hit);
                            if (ok) return;
                            fullRedraw = true;
                        }
                        handled = true;
                        continue;
                    }
                    if (insert) {
                        if (query.length() < 48) {
                            query += insert;
                            listDirty = true;
                        }
                        handled = true;
                    }
                }
                // FN+Space and other FN combos: ignore (don't type garbage).
                if (handled || key.word.size()) continue;
            } else {
                for (char c : key.word) {
                    if (c == ';') {
                        if (navOk && selected > 0) {
                            selected--;
                            lastNavMs = now;
                        }
                        handled = true;
                    } else if (c == '.') {
                        if (navOk && selected + 1 < (int)hits.size()) {
                            selected++;
                            lastNavMs = now;
                        }
                        handled = true;
                    } else if (c == ',') {
                        if (navOk) {
                            selected = max(0, selected - visible);
                            lastNavMs = now;
                        }
                        handled = true;
                    } else if (c == '/') {
                        if (navOk) {
                            selected = min((int)hits.size() - 1, selected + visible);
                            if (selected < 0) selected = 0;
                            lastNavMs = now;
                        }
                        handled = true;
                    }
                }
                if (handled) continue;
            }
        }

        // Backspace (short): delete one char.
        if (key.del && !backspaceLatched) {
            if (query.length()) {
                query.remove(query.length() - 1);
                listDirty = true;
            }
            continue;
        }

        // Printable characters (including Space). Skip arrow-key glyphs and HID codes.
        for (char c : key.word) {
            const uint8_t u = (uint8_t)c;
            if (u == 0xDA || u == 0xD9 || u == 0xD8 || u == 0xD7 || u == 0xB3 || u == 0xB1 || u == 0xD4)
                continue;
            if (c == ';' || c == '.' || c == ',' || c == '/') continue;
            if (c >= 32 && c < 127) {
                if (query.length() < 48) {
                    query += c;
                    listDirty = true;
                }
            }
        }
    }

    clearNavPulses();
    KeyStroke.Clear();
}

#endif // HAS_KEYBOARD
