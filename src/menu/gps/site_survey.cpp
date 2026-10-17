#if !defined(LITE_VERSION)
#include "site_survey.h"

#include "menu/wifi/channel_analyzer.h"
#include "root/app/utils.h"
#include "root/config/configPins.h"
#include "root/input/mykeyboard.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <TinyGPSPlus.h>
#include <WiFi.h>
#include <globals.h>

namespace {

HardwareSerial gpsSer(2);
TinyGPSPlus gps;

bool beginGps() {
    pinMode(kvxConfigPins.gps_bus.rx, INPUT);
    gpsSer.begin(kvxConfigPins.gpsBaudrate, SERIAL_8N1, kvxConfigPins.gps_bus.rx, kvxConfigPins.gps_bus.tx);
    uint32_t t0 = millis();
    displayTextLine("Waiting GPS...");
    while (millis() - t0 < 10000) {
        while (gpsSer.available()) gps.encode(gpsSer.read());
        if (gps.location.isValid()) return true;
        if (check(EscPress)) return false;
        delay(20);
    }
    // proceed even without fix
    return true;
}

} // namespace

static void siteSurveyHelp() {
    showAppHelp(
        {
            "Channel airtime survey with",
            "optional GPS stamps.",
            "",
            "Writes CSV under:",
            "  /kvxputer/app/WdBoth/",
            "  survey_<ms>.csv",
            "",
            "Columns:",
            "  time,lat,lon,alt,ch,",
            "  load_pct,peak_rssi,ap_count",
            "",
            "Uses channel analyzer dwells",
            "(promiscuous hop). GPS UART",
            "on Grove pins from Config.",
            "Without a fix, lat/lon are 0.",
            "",
            "ESC stops and saves.",
            "Distinct from Wardriving",
            "(Wigle AP rows).",
        }
    );
}

static void siteSurveyRun() {
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) {
        displayError("No storage", true);
        return;
    }
    kvx::paths::ensureDir(*fs, kvx::paths::GPS_WDBOTH);

    drawMainBorderWithTitle("Site Survey");
    if (!beginGps()) {
        displayError("GPS aborted", true);
        return;
    }

    if (!caSessionStart()) {
        gpsSer.end();
        displayError("Analyzer start fail", true);
        return;
    }

    char fname[80];
    snprintf(fname, sizeof(fname), "%s/survey_%lu.csv", kvx::paths::GPS_WDBOTH, (unsigned long)millis());

    File out = fs->open(fname, FILE_WRITE);
    if (!out) {
        caSessionStop();
        gpsSer.end();
        displayError("Create CSV fail", true);
        return;
    }
    out.println("time,lat,lon,alt,ch,load_pct,peak_rssi,ap_count");

    drawMainBorderWithTitle("Site Survey");
    padprintln(String(fname).substring(String(fname).lastIndexOf('/') + 1));
    padprintln("ESC stop");
    EscPress = false;
    int rows = 0;
    String paintedHud;
    while (!check(EscPress) && !returnToMenu && !forceHome) {
        while (gpsSer.available()) gps.encode(gpsSer.read());
        CaChannelSample sample;
        if (!caSessionDwell(120, sample, []() { return check(EscPress); })) break;

        double lat = gps.location.isValid() ? gps.location.lat() : 0;
        double lon = gps.location.isValid() ? gps.location.lng() : 0;
        double alt = gps.altitude.isValid() ? gps.altitude.meters() : 0;
        int apCount = 0;
        caSessionForEachAp([&](const CaApSample &) { apCount++; });

        char line[128];
        snprintf(
            line,
            sizeof(line),
            "%lu,%.6f,%.6f,%.1f,%u,%u,%d,%d",
            (unsigned long)millis(),
            lat,
            lon,
            alt,
            (unsigned)sample.ch,
            (unsigned)sample.load,
            (int)sample.rssi,
            apCount
        );
        out.println(line);
        rows++;
        String hud =
            "ch" + String(sample.ch) + " load" + String(sample.load) + "% n" + String(rows);
        if (hud != paintedHud) {
            tft.fillRect(10, 50, tftWidth - 20, 24, kvxConfig.bgColor);
            tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
            tft.drawString(hud, 10, 50);
            paintedHud = hud;
        }
    }
    out.close();
    caSessionStop();
    gpsSer.end();
    displayInfo("Saved " + String(rows) + " rows", true);
}

void siteSurveyMenu() {
    options = {
        {"Help",  siteSurveyHelp},
        {"Start", siteSurveyRun },
    };
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "Site Survey");
    options.clear();
}

#endif
