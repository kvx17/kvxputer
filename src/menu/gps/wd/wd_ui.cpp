#include "wd_ui.h"
#include "root/app/utils.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#include <globals.h>

void WdUi::reset() {
    current = WdDashView::Summary;
    chMode = 0;
    scroll = 0;
    liveFeed.clear();
    newFeed.clear();
}

void WdUi::nextView() {
    uint8_t v = (uint8_t)current + 1;
    if (v >= (uint8_t)WdDashView::Count) v = 0;
    current = (WdDashView)v;
    scroll = 0;
}

void WdUi::nextChannelMode() {
    chMode = (chMode + 1) % 3;
}

void WdUi::scrollUp() {
    if (scroll > 0) scroll--;
}

void WdUi::scrollDown() {
    scroll++;
}

void WdUi::pushRecent(const WdRecentAp &ap, bool uniqueOnlyFeed) {
    liveFeed.insert(liveFeed.begin(), ap);
    if (liveFeed.size() > FEED_MAX) liveFeed.resize(FEED_MAX);
    if (uniqueOnlyFeed || ap.isNew) {
        newFeed.insert(newFeed.begin(), ap);
        if (newFeed.size() > FEED_MAX) newFeed.resize(FEED_MAX);
    }
}

void WdUi::noteSecurity(wifi_auth_mode_t auth) {
    // counted by caller into session stats; helper kept for future
    (void)auth;
}

void WdUi::noteChannel(int ch, bool isUnique) {
    (void)ch;
    (void)isUnique;
}

void WdUi::clearSweepChannels() {}

void WdUi::showHelp() {
    showAppHelp(
        {
            "Wardriving keys:",
            "  H  help",
            "  B  blank display",
            "  S  mute beeps",
            "  P  pause CSV log",
            "  X  stop/start WiFi",
            "  F  FLAG marker",
            "  Q/ESC  safe exit",
            "  Sel  next dashboard",
            "  ,/.  channel map mode",
            "  ;/.  scroll (summary)",
            "",
            "Config: GPS > Wardriving",
            "  > WD Config",
            "CSV: /kvxputer/app/WdBoth",
        }
    );
}

void WdUi::drawLines(const char *title, const std::vector<String> &lines, const char *footer) {
    const int dense = uiDenseFont();
    const int rowH = uiLineH(dense) + 2;
    const int listY = KVX_TOPBAR_H + 4;
    const int footerH = uiLineH(dense) + 2;
    const int maxRows = max(1, (tftHeight - listY - footerH) / rowH);
    const int maxScroll = max(0, (int)lines.size() - maxRows);
    if (scroll > maxScroll) scroll = maxScroll;
    if (scroll < 0) scroll = 0;

    tft.fillScreen(kvxConfig.bgColor);
    drawKvxTopBar(title);

    tft.setTextSize(dense);
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    const int maxChars = max(1, (tftWidth - 12) / uiCharW(dense));
    for (int i = 0; i < maxRows && (scroll + i) < (int)lines.size(); i++) {
        String line = lines[scroll + i];
        if ((int)line.length() > maxChars) line = line.substring(0, maxChars);
        tft.setCursor(6, listY + i * rowH);
        tft.print(line);
    }

    String foot = footer;
    if (maxScroll > 0) {
        char hint[24];
        snprintf(hint, sizeof(hint), "  %d/%d", scroll + 1, (int)lines.size());
        foot += hint;
    }
    tft.setTextColor(kvxConfig.secColor, kvxConfig.bgColor);
    tft.drawCentreString(foot.c_str(), tftWidth / 2, tftHeight - footerH, 1);
}

void WdUi::draw(
    const char *title,
    const WdGpsFix &live,
    const WdGpsFix *logFix,
    bool canLog,
    const WdSessionStats &st,
    const WdLogger &logger,
    const WdConfig &cfg
) {
    std::vector<String> lines;
    char buf[48];

    auto elapsed = [&]() {
        uint32_t sec = (millis() - st.startMs) / 1000;
        snprintf(buf, sizeof(buf), "ET %02lu:%02lu:%02lu", sec / 3600, (sec / 60) % 60, sec % 60);
        return String(buf);
    };

    switch (current) {
        case WdDashView::Summary: {
            if (logger.isOpen()) lines.push_back("File: " + logger.filename());
            else if (logger.hasPath()) lines.push_back("File: " + logger.filename() + " !");
            else lines.push_back("File: (none)");
            if (logger.writeErrors()) {
                snprintf(buf, sizeof(buf), "WriteErr: %lu", (unsigned long)logger.writeErrors());
                lines.push_back(buf);
            }
            if (st.loggingPaused) lines.push_back("LOG PAUSED");
            if (st.wifiStopped) lines.push_back("WIFI STOPPED");
            if (!st.statusNote.isEmpty()) lines.push_back(st.statusNote);
            snprintf(buf, sizeof(buf), "WiFi log: %lu", (unsigned long)st.wifiLogged);
            lines.push_back(buf);
            snprintf(buf, sizeof(buf), "BLE log: %lu", (unsigned long)st.bleLogged);
            lines.push_back(buf);
            snprintf(buf, sizeof(buf), "Unique: %u", (unsigned)logger.uniqueCount());
            lines.push_back(buf);
            snprintf(buf, sizeof(buf), "Sweeps: %lu", (unsigned long)logger.sweeps());
            lines.push_back(buf);
            snprintf(buf, sizeof(buf), "Dist: %.2f km", st.distance_m / 1000.0);
            lines.push_back(buf);
            lines.push_back(elapsed());
            snprintf(buf, sizeof(buf), "Sats:%d Acc:%.0fm", live.satellites, live.accuracy_m);
            lines.push_back(buf);
            const bool liveCoords =
                live.location_valid && (live.lat != 0.0 || live.lng != 0.0);
            if (liveCoords) {
                snprintf(buf, sizeof(buf), "%.5f %.5f", live.lat, live.lng);
                lines.push_back(buf);
                lines.push_back(live.accuracy_ok ? "GPS: FIX OK" : "GPS: COORDS (soft)");
            } else if (canLog && logFix && (logFix->lat != 0.0 || logFix->lng != 0.0)) {
                snprintf(buf, sizeof(buf), "GPS:%s", st.gpsModeLabel.c_str());
                lines.push_back(buf);
                snprintf(buf, sizeof(buf), "%.5f %.5f", logFix->lat, logFix->lng);
                lines.push_back(buf);
            } else if (canLog && logFix) {
                lines.push_back("GPS: logging 0,0");
                lines.push_back(String("Mode: ") + cfg.gps_log_mode);
            } else {
                lines.push_back("GPS: no fix");
                lines.push_back(String("Mode: ") + cfg.gps_log_mode);
            }
            if (st.inGeofence) lines.push_back("IN GEOFENCE");
            if (st.alerts) {
                snprintf(buf, sizeof(buf), "Alerts: %lu", (unsigned long)st.alerts);
                lines.push_back(buf);
            }
            break;
        }
        case WdDashView::Security: {
            lines.push_back("Security snapshot");
            snprintf(buf, sizeof(buf), "Open: %d", st.openCount);
            lines.push_back(buf);
            snprintf(buf, sizeof(buf), "WPA2: %d", st.wpa2Count);
            lines.push_back(buf);
            snprintf(buf, sizeof(buf), "WPA3: %d", st.wpa3Count);
            lines.push_back(buf);
            snprintf(buf, sizeof(buf), "Other: %d", st.otherSec);
            lines.push_back(buf);
            snprintf(buf, sizeof(buf), "Blocked: %lu", (unsigned long)st.blocked);
            lines.push_back(buf);
            lines.push_back("Top recent:");
            for (size_t i = 0; i < liveFeed.size() && i < 3; i++) {
                snprintf(buf, sizeof(buf), "%ddBm %s", liveFeed[i].rssi, liveFeed[i].ssid.c_str());
                lines.push_back(buf);
            }
            break;
        }
        case WdDashView::Live: {
            lines.push_back("Live feed");
            for (const auto &a : liveFeed) {
                String s = a.ssid.length() ? a.ssid : a.bssid;
                snprintf(buf, sizeof(buf), "%ddBm %s", a.rssi, s.c_str());
                lines.push_back(buf);
            }
            if (liveFeed.empty()) lines.push_back("(empty)");
            break;
        }
        case WdDashView::NewAps: {
            lines.push_back("New APs");
            for (const auto &a : newFeed) {
                String s = a.ssid.length() ? a.ssid : a.bssid;
                snprintf(buf, sizeof(buf), "%ddBm %s", a.rssi, s.c_str());
                lines.push_back(buf);
            }
            if (newFeed.empty()) lines.push_back("(empty)");
            break;
        }
        case WdDashView::Channels: {
            const char *modeName = chMode == 0 ? "session" : (chMode == 1 ? "sweep" : "unique");
            snprintf(buf, sizeof(buf), "Channels (%s)", modeName);
            lines.push_back(buf);
            lines.push_back(",/. cycle mode");
            const uint32_t *src =
                chMode == 0 ? st.channelSession : (chMode == 1 ? st.channelSweep : st.channelUnique);
            uint32_t peak = 1;
            for (int ch = 1; ch <= 13; ch++)
                if (src[ch] > peak) peak = src[ch];
            for (int ch = 1; ch <= 13; ch++) {
                int bars = (int)((src[ch] * 10) / peak);
                String row = String(ch);
                if (ch < 10) row = " " + row;
                row += "|";
                for (int b = 0; b < bars; b++) row += "#";
                snprintf(buf, sizeof(buf), " %lu", (unsigned long)src[ch]);
                row += buf;
                lines.push_back(row);
            }
            break;
        }
        case WdDashView::System: {
            lines.push_back("System");
            snprintf(buf, sizeof(buf), "Bat: %d%%", getBattery());
            lines.push_back(buf);
            snprintf(buf, sizeof(buf), "Heap: %u", (unsigned)ESP.getFreeHeap());
            lines.push_back(buf);
            lines.push_back(String("Scan: ") + cfg.scan_mode);
            lines.push_back(String("GPS: ") + cfg.gps_log_mode);
            lines.push_back(st.muted ? "Sound: MUTED" : "Sound: ON");
            lines.push_back(st.displayBlank ? "Disp: BLANK" : "Disp: ON");
            snprintf(buf, sizeof(buf), "CC:%s TX:%d", cfg.wifi_country_code.c_str(), cfg.wifi_tx_power);
            lines.push_back(buf);
            lines.push_back(elapsed());
            break;
        }
        default: break;
    }

    const char *footer = "Sel dash  ;/. scroll  ESC";
    drawLines(title, lines, footer);
}
