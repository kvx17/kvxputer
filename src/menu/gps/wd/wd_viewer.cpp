#include "wd_viewer.h"
#include "root/input/mykeyboard.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#include <FS.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <globals.h>
#include <unordered_map>
#include <vector>

namespace {

constexpr const char *OUT_DIR = kvx::paths::GPS_WDBOTH;
constexpr size_t MAX_ROWS = 800;
constexpr int FOOTER_H = 14;

struct WdCapRow {
    String bssid;
    String ssid;
    String auth;
    String firstSeen;
    int channel = 0;
    int rssi = 0;
    double lat = 0;
    double lng = 0;
    String type; // WIFI / BLE
};

struct LoadStats {
    int linesRead = 0;
    int dataLines = 0;
    int parsed = 0;
    int wifi = 0;
    int ble = 0;
    bool openOk = false;
};

static int bodyTop() { return KVX_TOPBAR_H + 2; }
static int bodyBottom() { return tftHeight - FOOTER_H; }
static int rowH() { return uiLineH(uiDenseFont()) + 1; }
static int visibleRows() { return max(1, (bodyBottom() - bodyTop()) / rowH()); }

static String trimQuotes(String s) {
    s.trim();
    // Strip UTF-8 BOM if present on first field
    if (s.length() >= 3 && (uint8_t)s[0] == 0xEF && (uint8_t)s[1] == 0xBB && (uint8_t)s[2] == 0xBF) {
        s.remove(0, 3);
        s.trim();
    }
    if (s.length() >= 2 && s[0] == '"' && s[s.length() - 1] == '"') {
        s = s.substring(1, s.length() - 1);
        // Unescape doubled quotes
        s.replace("\"\"", "\"");
    }
    return s;
}

static std::vector<String> splitCsvLine(const String &line) {
    std::vector<String> fields;
    String cur;
    bool inQuotes = false;
    for (size_t i = 0; i < line.length(); i++) {
        char c = line[i];
        if (c == '"') {
            inQuotes = !inQuotes;
            cur += c;
            continue;
        }
        if (c == ',' && !inQuotes) {
            fields.push_back(cur);
            cur = "";
            continue;
        }
        cur += c;
    }
    fields.push_back(cur);
    return fields;
}

static bool parseMacToU64(const String &mac, uint64_t &out) {
    uint64_t value = 0;
    int nibbles = 0;
    for (size_t i = 0; i < mac.length(); i++) {
        char c = mac[i];
        if (c == ':' || c == '-') continue;
        if (!isxdigit((unsigned char)c)) return false;
        value <<= 4;
        if (c >= '0' && c <= '9') value |= (uint64_t)(c - '0');
        else if (c >= 'a' && c <= 'f') value |= (uint64_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') value |= (uint64_t)(c - 'A' + 10);
        else return false;
        nibbles++;
        if (nibbles > 12) return false;
    }
    if (nibbles != 12) return false;
    out = value;
    return true;
}

static uint64_t fallbackKey(const String &s) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < s.length(); i++) {
        h ^= (uint8_t)s[i];
        h *= 1099511628211ULL;
    }
    return h ? h : 1;
}

static bool isHeaderOrMeta(const String &line) {
    if (line.startsWith("WigleWifi")) return true;
    String upper = line;
    upper.toUpperCase();
    if (upper.startsWith("MAC,SSID")) return true;
    if (upper.startsWith("MAC, SSID")) return true;
    return false;
}

static bool looksLikeMac(const String &s) {
    uint64_t ignore = 0;
    return parseMacToU64(s, ignore);
}

static bool parseNetworkRow(const String &line, WdCapRow &out) {
    auto f = splitCsvLine(line);
    if (f.size() < 7) return false;

    String mac = trimQuotes(f[0]);
    if (!looksLikeMac(mac)) return false;

    // Type is usually the last non-empty field in WiGLE CSV v1.6
    String type;
    for (int i = (int)f.size() - 1; i >= 0; i--) {
        String t = trimQuotes(f[i]);
        t.toUpperCase();
        if (t == "WIFI" || t == "BLE") {
            type = t;
            break;
        }
    }
    String ssid = trimQuotes(f.size() > 1 ? f[1] : "");
    String auth = trimQuotes(f.size() > 2 ? f[2] : "");
    if (ssid == "FLAG" || auth.indexOf("FLAG") >= 0) return false;

    // Infer type when older / truncated rows omit it
    if (type.isEmpty()) {
        String authU = auth;
        authU.toUpperCase();
        type = (authU.indexOf("BLE") >= 0) ? "BLE" : "WIFI";
    }

    out.bssid = mac;
    out.ssid = ssid;
    out.auth = auth;
    if (out.auth.startsWith("[") && out.auth.endsWith("]") && out.auth.length() >= 2) {
        out.auth = out.auth.substring(1, out.auth.length() - 1);
    }
    out.firstSeen = trimQuotes(f.size() > 3 ? f[3] : "");
    out.channel = trimQuotes(f.size() > 4 ? f[4] : "").toInt();
    // RSSI is column 6 in the standard layout (0=MAC … 6=RSSI)
    out.rssi = trimQuotes(f.size() > 6 ? f[6] : "").toInt();
    out.lat = trimQuotes(f.size() > 7 ? f[7] : "").toDouble();
    out.lng = trimQuotes(f.size() > 8 ? f[8] : "").toDouble();
    out.type = type;
    return true;
}

static String shortAuth(const String &auth) {
    String a = auth;
    a.toUpperCase();
    if (a.indexOf("BLE") >= 0) return "BLE";
    if (a.indexOf("OPEN") >= 0) return "OPEN";
    if (a.indexOf("WEP") >= 0) return "WEP";
    if (a.indexOf("WPA3") >= 0) return "WPA3";
    if (a.indexOf("WPA2") >= 0) return "WPA2";
    if (a.indexOf("WPA") >= 0) return "WPA";
    if (a.length() > 6) return a.substring(0, 6);
    return a.length() ? a : "?";
}

static String truncPad(const String &s, int width) {
    if (width <= 0) return "";
    if ((int)s.length() >= width) return s.substring(0, width);
    String out = s;
    while ((int)out.length() < width) out += ' ';
    return out;
}

static void drawFooter(const char *text) {
    const int dense = uiDenseFont();
    tft.fillRect(0, bodyBottom(), tftWidth, FOOTER_H, kvxConfig.bgColor);
    tft.drawFastHLine(0, bodyBottom(), tftWidth, kvxConfig.priColor);
    tft.setTextSize(dense);
    tft.setTextColor(kvxConfig.secColor, kvxConfig.bgColor);
    tft.drawCentreString(text, tftWidth / 2, bodyBottom() + 2, 1);
}

static LoadStats loadCsvUnique(FS &fs, const String &path, std::vector<WdCapRow> &rows) {
    LoadStats st;
    rows.clear();
    File f = fs.open(path, FILE_READ);
    if (!f) return st;
    st.openOk = true;

    std::unordered_map<uint64_t, size_t> indexByKey;
    while (f.available() && rows.size() < MAX_ROWS) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.isEmpty()) continue;
        st.linesRead++;
        if (isHeaderOrMeta(line)) continue;
        st.dataLines++;

        WdCapRow row;
        if (!parseNetworkRow(line, row)) continue;
        st.parsed++;
        if (row.type == "WIFI") st.wifi++;
        else st.ble++;

        uint64_t key = 0;
        if (!parseMacToU64(row.bssid, key)) key = fallbackKey(row.bssid + "|" + row.type);

        auto it = indexByKey.find(key);
        if (it == indexByKey.end()) {
            indexByKey[key] = rows.size();
            rows.push_back(row);
        } else {
            WdCapRow &prev = rows[it->second];
            if (row.rssi > prev.rssi) prev = row;
        }
    }
    f.close();

    std::sort(rows.begin(), rows.end(), [](const WdCapRow &a, const WdCapRow &b) {
        if (a.rssi != b.rssi) return a.rssi > b.rssi;
        return a.ssid < b.ssid;
    });
    return st;
}

static void showDetail(const WdCapRow &r, int idx, int total) {
    std::vector<String> lines;
    char buf[48];
    snprintf(buf, sizeof(buf), "%s %d / %d", r.type.c_str(), idx + 1, total);
    lines.push_back(buf);
    lines.push_back("SSID: " + (r.ssid.length() ? r.ssid : "(hidden)"));
    lines.push_back("BSSID: " + r.bssid);
    lines.push_back("Auth: " + r.auth);
    snprintf(buf, sizeof(buf), "Ch: %d  RSSI: %d", r.channel, r.rssi);
    lines.push_back(buf);
    lines.push_back("Seen: " + r.firstSeen);
    snprintf(buf, sizeof(buf), "Lat: %.5f", r.lat);
    lines.push_back(buf);
    snprintf(buf, sizeof(buf), "Lon: %.5f", r.lng);
    lines.push_back(buf);

    const int dense = uiDenseFont();
    const int rh = uiLineH(dense) + 2;
    const int y0 = bodyTop();
    tft.fillScreen(kvxConfig.bgColor);
    drawKvxTopBar("WD Detail");
    tft.setTextSize(dense);
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    const int maxChars = max(1, (tftWidth - 12) / uiCharW(dense));
    for (size_t i = 0; i < lines.size(); i++) {
        String line = lines[i];
        if ((int)line.length() > maxChars) line = line.substring(0, maxChars);
        tft.setCursor(6, y0 + (int)i * rh);
        tft.print(line);
    }
    drawFooter("Esc back");

    while (!check(EscPress) && !returnToMenu && !forceHome) {
        vTaskDelay(40 / portTICK_PERIOD_MS);
    }
}

static void showTable(const String &fileLabel, std::vector<WdCapRow> &rows) {
    int cursor = 0;
    int scroll = 0;
    bool dirty = true;

    while (!returnToMenu && !forceHome) {
        if (check(EscPress)) break;
        if (check(UpPress) || check(PrevPress)) {
            if (cursor > 0) {
                cursor--;
                dirty = true;
            }
        } else if (check(DownPress) || check(NextPress)) {
            if (cursor < (int)rows.size() - 1) {
                cursor++;
                dirty = true;
            }
        } else if (check(SelPress)) {
            showDetail(rows[cursor], cursor, (int)rows.size());
            dirty = true;
        }

        const int vis = visibleRows() - 1;
        if (cursor < scroll) scroll = cursor;
        if (cursor >= scroll + vis) scroll = cursor - vis + 1;
        if (scroll < 0) scroll = 0;

        if (!dirty) {
            vTaskDelay(30 / portTICK_PERIOD_MS);
            continue;
        }
        dirty = false;

        const int dense = uiDenseFont();
        const int rh = rowH();
        tft.fillScreen(kvxConfig.bgColor);
        char status[24];
        snprintf(status, sizeof(status), "%d/%d", cursor + 1, (int)rows.size());
        drawKvxTopBar(fileLabel.c_str(), status);

        tft.setTextSize(dense);
        const int cw = uiCharW(dense);
        const int maxChars = max(1, (tftWidth - 8) / cw);

        tft.setTextColor(kvxConfig.secColor, kvxConfig.bgColor);
        String header = truncPad("#", 3) + truncPad("RSSI", 5) + truncPad("CH", 3) + truncPad("AUTH", 5) +
                        "SSID";
        if ((int)header.length() > maxChars) header = header.substring(0, maxChars);
        tft.setCursor(4, bodyTop());
        tft.print(header);
        tft.drawFastHLine(0, bodyTop() + rh - 1, tftWidth, kvxConfig.secColor);

        for (int i = 0; i < vis; i++) {
            int idx = scroll + i;
            if (idx >= (int)rows.size()) break;
            const WdCapRow &r = rows[idx];
            const bool sel = (idx == cursor);
            const uint16_t fg = sel ? kvxConfig.bgColor : kvxConfig.priColor;
            const uint16_t bg = sel ? kvxConfig.priColor : kvxConfig.bgColor;
            const int y = bodyTop() + (i + 1) * rh;
            tft.fillRect(0, y, tftWidth, rh, bg);
            tft.setTextColor(fg, bg);

            char num[8];
            snprintf(num, sizeof(num), "%d", idx + 1);
            char rssi[8];
            snprintf(rssi, sizeof(rssi), "%d", r.rssi);
            char ch[8];
            snprintf(ch, sizeof(ch), "%d", r.channel);

            String ssid = r.ssid.length() ? r.ssid : "(hidden)";
            if (r.type == "BLE" && ssid == "(hidden)") ssid = "(ble)";
            String line = truncPad(num, 3) + truncPad(rssi, 5) + truncPad(ch, 3) +
                          truncPad(shortAuth(r.auth), 5) + ssid;
            if ((int)line.length() > maxChars) line = line.substring(0, maxChars);
            tft.setCursor(4, y);
            tft.print(line);
        }

        drawFooter(";/. move  OK detail  ESC");
    }
}

static void viewOneFile(FS &fs, const String &path, const String &name) {
    displayTextLine("Loading...");
    std::vector<WdCapRow> rows;
    LoadStats st = loadCsvUnique(fs, path, rows);

    if (!st.openOk) {
        displayError("Cannot open file", true);
        return;
    }
    if (rows.empty()) {
        size_t sz = 0;
        File probe = fs.open(path, FILE_READ);
        if (probe) {
            sz = probe.size();
            probe.close();
        }
        char msg[128];
        if (st.dataLines == 0) {
            snprintf(
                msg,
                sizeof(msg),
                "Empty capture\n%d hdr lines %uB\nNeed GPS fix\nor gps=zero_gps",
                st.linesRead,
                (unsigned)sz
            );
        } else {
            snprintf(
                msg,
                sizeof(msg),
                "Parse failed\nlines:%d data:%d\nparsed:0",
                st.linesRead,
                st.dataLines
            );
        }
        displayError(msg, true);
        return;
    }

    String label = name;
    if (label.endsWith(".csv") || label.endsWith(".CSV")) label.remove(label.length() - 4);
    if (label.length() > 14) label = label.substring(0, 14);
    showTable(label, rows);
}

} // namespace

void wardrivingViewCaptureFile(FS &fs, const String &path, const String &name) {
    viewOneFile(fs, path, name);
}

void wardrivingViewCaptures() {
    FS *fs = nullptr;
    setupSdCard();
    if (!getFsStorage(fs) || !fs) {
        displayError("No storage", true);
        return;
    }
    kvx::paths::ensureDir(*fs, OUT_DIR);

    // Keep full path as stored by the FS — reconstructing can fail on some SD stacks.
    struct CapFile {
        String path;
        String name;
    };
    std::vector<CapFile> files;
    File root = fs->open(OUT_DIR);
    if (!root || !root.isDirectory()) {
        displayError("WdBoth folder missing", true);
        return;
    }
    while (true) {
        bool isDir = false;
        String full = root.getNextFileName(&isDir);
        if (full.isEmpty()) break;
        if (isDir) continue;
        String name = full.substring(full.lastIndexOf('/') + 1);
        String lower = name;
        lower.toLowerCase();
        if (!lower.endsWith(".csv")) continue;
        if (lower == "stats.csv") continue;
        String path = full;
        if (full.indexOf('/') < 0) path = String(OUT_DIR) + "/" + name;
        files.push_back({path, name});
    }
    root.close();

    if (files.empty()) {
        displayError("No CSV captures\nin WdBoth", true);
        return;
    }

    std::sort(files.begin(), files.end(), [](const CapFile &a, const CapFile &b) {
        return a.name > b.name;
    });

    while (!returnToMenu && !forceHome) {
        options.clear();
        for (size_t i = 0; i < files.size(); i++) {
            // Capture by value — CapFile copies are small.
            CapFile cap = files[i];
            options.push_back({cap.name, [fs, cap]() { viewOneFile(*fs, cap.path, cap.name); }});
        }
        options.push_back({"Back", []() { returnToMenu = true; }});
        returnToMenu = false;
        loopOptions(options, MENU_TYPE_SUBMENU, "WD Caps");
        if (returnToMenu) break;
    }
    returnToMenu = false;
}
