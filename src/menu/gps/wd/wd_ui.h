#pragma once

#include "wd_config.h"
#include "wd_gps.h"
#include "wd_logger.h"
#include "wd_scanner.h"
#include <Arduino.h>
#include <vector>

enum class WdDashView : uint8_t {
    Summary = 0,
    Security,
    Live,
    NewAps,
    Channels,
    System,
    Count
};

struct WdRecentAp {
    String ssid;
    String bssid;
    int rssi = 0;
    int channel = 0;
    wifi_auth_mode_t auth = WIFI_AUTH_OPEN;
    String time;
    bool isNew = false;
};

struct WdSessionStats {
    uint32_t wifiLogged = 0;
    uint32_t bleLogged = 0;
    uint32_t wifiSeen = 0;
    uint32_t bleSeen = 0;
    uint32_t alerts = 0;
    uint32_t flags = 0;
    uint32_t blocked = 0;
    int openCount = 0;
    int wpa2Count = 0;
    int wpa3Count = 0;
    int otherSec = 0;
    uint32_t channelSession[14] = {};
    uint32_t channelSweep[14] = {};
    uint32_t channelUnique[14] = {};
    double distance_m = 0;
    uint32_t startMs = 0;
    bool loggingPaused = false;
    bool wifiStopped = false;
    bool displayBlank = false;
    bool muted = false;
    bool inGeofence = false;
    String gpsModeLabel;
    String statusNote;
};

class WdUi {
public:
    void reset();
    void nextView();
    void nextChannelMode();
    WdDashView view() const { return current; }
    uint8_t channelMode() const { return chMode; }

    void pushRecent(const WdRecentAp &ap, bool uniqueOnlyFeed);
    void noteSecurity(wifi_auth_mode_t auth);
    void noteChannel(int ch, bool isUnique);

    void clearSweepChannels();

    void draw(
        const char *title,
        const WdGpsFix &live,
        const WdGpsFix *logFix,
        bool canLog,
        const WdSessionStats &st,
        const WdLogger &logger,
        const WdConfig &cfg
    );

    void showHelp();

private:
    WdDashView current = WdDashView::Summary;
    uint8_t chMode = 0; // 0 session 1 sweep 2 unique
    std::vector<WdRecentAp> liveFeed;
    std::vector<WdRecentAp> newFeed;
    static constexpr size_t FEED_MAX = 10;

    void drawLines(const char *title, const std::vector<String> &lines, const char *footer);
};
