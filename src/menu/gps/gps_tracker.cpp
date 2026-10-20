/**
 * @file gps_tracker.cpp
 * @author Rennan Cockles (https://github.com/rennancockles)
 * @brief GPS tracker
 * @version 0.1
 * @date 2024-11-20
 */

#include "gps_tracker.h"
#include "menu/gps/gps_hw.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#include "root/ui/scrollableTextArea.h"
#include "root/input/mykeyboard.h"
#include "root/storage/sd_functions.h"
#include "current_year.h"
#include <cstdlib>
#include <vector>

namespace {

double nmeaToDeg(const String &raw, char hemi) {
    if (raw.length() < 3) return 0;
    // NMEA ddmm.mmm / dddmm.mmm
    double whole = raw.toDouble();
    int deg = (int)(whole / 100.0);
    double minutes = whole - (deg * 100.0);
    double d = deg + minutes / 60.0;
    if (hemi == 'S' || hemi == 'W') d = -d;
    return d;
}

std::vector<String> splitNmea(const String &line) {
    std::vector<String> out;
    int start = 0;
    for (int i = 0; i < (int)line.length(); i++) {
        if (line[i] == ',' || line[i] == '*') {
            out.push_back(line.substring(start, i));
            start = i + 1;
            if (line[i] == '*') break;
        }
    }
    return out;
}

} // namespace

GPSTracker::GPSTracker() { setup(); }

GPSTracker::~GPSTracker() {
    add_final_file_data();
    if (gpsConnected) end();
    ioExpander.turnPinOnOff(IO_EXP_GPS, LOW);
#ifdef USE_BOOST
    PPM.disableOTG();
#endif
}

void GPSTracker::setup() {
    ioExpander.turnPinOnOff(IO_EXP_GPS, HIGH);
#ifdef USE_BOOST /// ENABLE 5V OUTPUT
    PPM.enableOTG();
#endif
    tft.fillScreen(kvxConfig.bgColor);
    drawKvxTopBar("GPS Tracker");
    displayTextLine("Initializing...");

    if (!begin_gps()) return;

    return loop();
}

bool GPSTracker::begin_gps() {
    releasePins();
    GpsHwInfo hw = gpsHwPrepare(GPSserial);
    displayTextLine(String(hw.label) + " @" + String(hw.baud));

    int count = 0;
    while (GPSserial.available() <= 0) {
        if (check(EscPress)) {
            end();
            return false;
        }
        displayTextLine("Waiting GPS: " + String(count) + "s");
        count++;
        if (count > 12) {
            hw = gpsHwPrepare(GPSserial);
            count = 0;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    gpsConnected = true;
    return true;
}

void GPSTracker::end() {
    GPSserial.end();
    restorePins();

    returnToMenu = true;
    gpsConnected = false;
}

void GPSTracker::noteNmeaChar(char c) {
    if (c == '\r') return;
    if (c == '\n') {
        if (nmeaLine.length() > 6 && nmeaLine[0] == '$') {
            auto f = splitNmea(nmeaLine);
            if (f.size() >= 7) {
                String type = f[0];
                type.toUpperCase();
                if (type.endsWith("GGA") && f.size() >= 8) {
                    int q = f[6].length() ? f[6].toInt() : 0;
                    fbQuality = q;
                    fbSats = f[7].length() ? f[7].toInt() : fbSats;
                    if (q > 0 && f[2].length() && f[4].length()) {
                        fbLat = nmeaToDeg(f[2], f[3].length() ? f[3][0] : 'N');
                        fbLng = nmeaToDeg(f[4], f[5].length() ? f[5][0] : 'E');
                        fbFixMs = millis();
                    }
                } else if (type.endsWith("RMC") && f.size() >= 7) {
                    fbRmcStatus = f[2].length() ? f[2][0] : '-';
                    if ((fbRmcStatus == 'A' || fbRmcStatus == 'a') && f[3].length() && f[5].length()) {
                        fbLat = nmeaToDeg(f[3], f[4].length() ? f[4][0] : 'N');
                        fbLng = nmeaToDeg(f[5], f[6].length() ? f[6][0] : 'E');
                        fbFixMs = millis();
                        fbQuality = max(fbQuality, 1);
                    }
                }
            }
        }
        nmeaLine = "";
        return;
    }
    if (nmeaLine.length() < 120) nmeaLine += c;
    else nmeaLine = "";
}

bool GPSTracker::fallbackFixFresh() const {
    return fbFixMs != 0 && (millis() - fbFixMs) < 5000 && (fbLat != 0.0 || fbLng != 0.0) && fbQuality > 0;
}

bool GPSTracker::saneGpsDateTime() {
    if (!gps.date.isValid() || !gps.time.isValid()) return false;
    const int y = gps.date.year();
    const int m = gps.date.month();
    const int d = gps.date.day();
    const int hh = gps.time.hour();
    const int mm = gps.time.minute();
    const int ss = gps.time.second();
    return y >= 2020 && y < 2100 && m >= 1 && m <= 12 && d >= 1 && d <= 31 && hh < 24 && mm < 60 && ss < 60;
}

void GPSTracker::loop() {
    int idleSeconds = 0;
    uint32_t lastUiMs = 0;
    uint32_t lastCoordMs = 0;
    uint32_t chars = 0;
    uint32_t sentences = 0;
    returnToMenu = false;

    tft.fillScreen(kvxConfig.bgColor);
    drawKvxTopBar("GPS Tracker");

    const int bodyY = KVX_TOPBAR_H + 2;
    const int bodyH = max(20, tftHeight - bodyY - 2);
    ScrollableTextArea area(
        uiDenseFont(), BORDER_PAD_X, bodyY, tftWidth - 2 * BORDER_PAD_X, bodyH, false, false
    );

    const uint16_t labelColor = kvxConfig.priColor;
    const uint16_t valueColor = kvxConfig.secColor;
    const uint8_t labelFont = uiDenseFont();
    const uint8_t valueFont = uiBodyFont();

    auto rebuildHud = [&](bool locValid, bool useFallback, int sats) {
        const size_t keepScroll = area.firstVisibleLine;
        area.clear();

        auto addField = [&](const char *label, const String &value) {
            area.addLine(label, labelColor, labelFont);
            area.addLine(value, valueColor, valueFont);
        };

        char buf[48];
        snprintf(
            buf,
            sizeof(buf),
            "RX%d TX%d @%d",
            (int)kvxConfigPins.gps_bus.rx,
            (int)kvxConfigPins.gps_bus.tx,
            kvxConfigPins.gpsBaudrate
        );
        addField("UART:", buf);

        snprintf(buf, sizeof(buf), "%lu / %lu", (unsigned long)chars, (unsigned long)sentences);
        addField("Chars / NMEA:", buf);

        snprintf(buf, sizeof(buf), "%d", sats);
        addField("Satellites:", buf);

        snprintf(buf, sizeof(buf), "%d", fbQuality);
        addField("Fix quality:", buf);

        snprintf(buf, sizeof(buf), "%c", fbRmcStatus);
        addField("RMC status:", buf);

        if (locValid) {
            snprintf(buf, sizeof(buf), "%.6f", gps.location.lat());
            addField("Latitude:", buf);
            snprintf(buf, sizeof(buf), "%.6f", gps.location.lng());
            addField("Longitude:", buf);
        } else if (useFallback) {
            snprintf(buf, sizeof(buf), "%.6f", fbLat);
            addField("Latitude:", buf);
            snprintf(buf, sizeof(buf), "%.6f", fbLng);
            addField("Longitude:", buf);
        } else {
            addField("Fix:", "No fix yet");
            addField("Hint:", sats <= 0 ? "Need clear sky" : "Acquiring...");
        }

        if (saneGpsDateTime()) {
            date_time_updated = true;
            snprintf(
                buf,
                sizeof(buf),
                "%04d-%02d-%02d",
                gps.date.year(),
                gps.date.month(),
                gps.date.day()
            );
            addField("Date:", buf);
            snprintf(
                buf,
                sizeof(buf),
                "%02d:%02d:%02d UTC",
                gps.time.hour(),
                gps.time.minute(),
                gps.time.second()
            );
            addField("Time:", buf);
        } else {
            addField("Date/Time:", "Waiting...");
        }

        snprintf(buf, sizeof(buf), "%.1f", gps.hdop.isValid() ? gps.hdop.hdop() : -1.0);
        addField("HDOP:", buf);

        snprintf(
            buf,
            sizeof(buf),
            "%lu / %lu",
            (unsigned long)gps.passedChecksum(),
            (unsigned long)gps.failedChecksum()
        );
        addField("NMEA ok/bad:", buf);

        if (gpsCoordCount > 0) {
            addField("Track points:", String(gpsCoordCount));
            snprintf(buf, sizeof(buf), "%.2f km", distance / 1000.0);
            addField("Distance:", buf);
            if (filename.length()) {
                String shortName = filename;
                if (shortName.endsWith(".gpx")) shortName.remove(shortName.length() - 4);
                addField("File:", shortName);
            }
        }

        addField("Keys:", ";/. scroll  Esc exit");

        if (keepScroll < area.getMaxLines()) area.firstVisibleLine = keepScroll;
        else area.firstVisibleLine = area.getMaxLines() ? area.getMaxLines() - 1 : 0;
        area.draw(true);
    };

    rebuildHud(false, false, 0);

    while (1) {
        if (check(EscPress) || returnToMenu) return end();

        while (GPSserial.available() > 0) {
            char c = (char)GPSserial.read();
            chars++;
            noteNmeaChar(c);
            if (gps.encode(c)) sentences++;
            idleSeconds = 0;
        }

        const bool locValid =
            gps.location.isValid() && (gps.location.lat() != 0.0 || gps.location.lng() != 0.0);
        const uint32_t locAge = gps.location.isValid() ? gps.location.age() : 0xFFFFFFFF;
        const bool useFallback = !locValid && fallbackFixFresh();
        const bool shouldLog = (lastCoordMs == 0 || millis() - lastCoordMs >= 2000) &&
                               ((locValid && locAge < 5000) || useFallback);

        if (shouldLog) {
            if (locValid) set_position();
            else {
                if (initial_position_set) distance += gps.distanceBetween(cur_lat, cur_lng, fbLat, fbLng);
                else initial_position_set = true;
                cur_lat = fbLat;
                cur_lng = fbLng;
            }
            add_coord();
            lastCoordMs = millis();
        }

        if (filename == "" && saneGpsDateTime() && gps.date.year() >= CURRENT_YEAR &&
            gps.date.year() < CURRENT_YEAR + 5) {
            create_filename();
        }

        // Scroll without blocking UART pump (;/. / encoder).
        area.updateSoft(false);

        if (millis() - lastUiMs >= 500) {
            lastUiMs = millis();
            const int sats = gps.satellites.isValid() ? (int)gps.satellites.value() : fbSats;
            char status[16];
            if (locValid || useFallback) snprintf(status, sizeof(status), "FIX");
            else if (sats > 0) snprintf(status, sizeof(status), "ACQ");
            else snprintf(status, sizeof(status), "NOFIX");
            drawKvxTopBar("GPS Tracker", status);
            rebuildHud(locValid, useFallback, sats);

            if (chars == 0) {
                idleSeconds++;
                if (idleSeconds >= 8) {
                    displayError(
                        "No UART data\nGPS > Config > Source\nCap RX15/TX13\nGrove RX1/TX2\n115200",
                        true
                    );
                    return end();
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void GPSTracker::set_position() {
    double lat = gps.location.lat();
    double lng = gps.location.lng();

    if (initial_position_set) distance += gps.distanceBetween(cur_lat, cur_lng, lat, lng);
    else initial_position_set = true;

    cur_lat = lat;
    cur_lng = lng;
}

void GPSTracker::create_filename() {
    char timestamp[20];
    sprintf(
        timestamp,
        "%02d%02d%02d_%02d%02d%02d",
        gps.date.year() % 100,
        gps.date.month() % 100,
        gps.date.day() % 100,
        gps.time.hour() % 100,
        gps.time.minute() % 100,
        gps.time.second() % 100
    );
    filename = String(timestamp) + "_gps_tracker.gpx";
}

void GPSTracker::add_initial_file_data(File file) {
    file.println("<?xml version=\"1.0\" encoding=\"ISO-8859-1\" standalone=\"yes\"?>");
    file.println("<?xml-stylesheet type=\"text/xsl\" href=\"details.xsl\"?>");
    file.println("<gpx");
    file.println("  version=\"1.1\"");
    file.println("  creator=\"Bruce Firmware\"");
    file.println("  xmlns=\"http://www.topografix.com/GPX/1/1\"");
    file.println("  xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"");
    file.println(
        "  xsi:schemaLocation=\"http://www.topografix.com/GPX/1/1 http://www.topografix.com/GPX/1/1/gpx.xsd\""
    );
    file.println(">");
    file.println("  <metadata>");
    file.println("    <name>Bruce GPS Tracker</name>");
    file.println("    <desc>GPS Tracker using Bruce Firmware</desc>");
    file.println("    <link href=\"https://github.com/kvx17/kvxputer\">");
    file.println("      <text>Bruce Website</text>");
    file.println("    </link>");
    file.println("  </metadata>");
    file.println("  <trk>");
    file.println("    <name>Bruce Route</name>");
    file.println("    <desc>GPS route captured by Bruce firmware</desc>");
    file.println("    <trkseg>");
}

void GPSTracker::add_final_file_data() {
    FS *fs;
    if (!getFsStorage(fs)) return;
    if (filename == "" || !(*fs).exists((String(kvx::paths::GPS_TRACKS) + "/") + filename)) return;

    File file = (*fs).open((String(kvx::paths::GPS_TRACKS) + "/") + filename, FILE_APPEND);

    if (!file) return;
    file.println("    </trkseg>");
    file.println("  </trk>");
    file.println("</gpx>");

    file.close();
}

void GPSTracker::add_coord() {
    FS *fs;
    if (!getFsStorage(fs)) {
        padprintln("Storage setup error");
        returnToMenu = true;
        return;
    }

    if (filename == "") create_filename();

    if (!(*fs).exists(kvx::paths::GPS_TRACKS)) (*fs).mkdir(kvx::paths::GPS_TRACKS);

    bool is_new_file = false;
    if (!(*fs).exists((String(kvx::paths::GPS_TRACKS) + "/") + filename)) is_new_file = true;
    File file = (*fs).open((String(kvx::paths::GPS_TRACKS) + "/") + filename, is_new_file ? FILE_WRITE : FILE_APPEND);

    if (!file) {
        padprintln("Failed to open file for writing");
        returnToMenu = true;
        return;
    }

    if (is_new_file) add_initial_file_data(file);

    file.printf("      <trkpt lat=\"%f\" lon=\"%f\">\n", cur_lat, cur_lng);
    file.println("        <sym>Waypoint</sym>");
    file.printf(
        "        <ele>%f</ele>\n", gps.altitude.isValid() ? gps.altitude.meters() : 0.0
    );
    file.printf("        <hdop>%f</hdop>\n", gps.hdop.hdop());
    file.printf("        <sat>%ld</sat>\n", gps.satellites.value());
    file.println("      </trkpt>");

    gpsCoordCount++;

    file.close();
}

void GPSTracker::releasePins() {
    rxPinReleased = false;
    if (kvxConfigPins.CC1101_bus.checkConflict(kvxConfigPins.gps_bus.rx) ||
        kvxConfigPins.NRF24_bus.checkConflict(kvxConfigPins.gps_bus.rx) ||
#if !defined(LITE_VERSION)
        kvxConfigPins.W5500_bus.checkConflict(kvxConfigPins.gps_bus.rx) ||
        kvxConfigPins.LoRa_bus.checkConflict(kvxConfigPins.gps_bus.rx) ||
#endif
        kvxConfigPins.SDCARD_bus.checkConflict(kvxConfigPins.gps_bus.rx)) {
        // T-Embed CC1101 and T-Display S3 Touch ties this pin to the NRF24 CS; switch it to input so the GPS
        // UART can drive it.
        pinMode(kvxConfigPins.gps_bus.rx, INPUT);
        rxPinReleased = true;
    }
}

void GPSTracker::restorePins() {
    if (rxPinReleased) {
        if (kvxConfigPins.CC1101_bus.checkConflict(kvxConfigPins.gps_bus.rx) ||
            kvxConfigPins.NRF24_bus.checkConflict(kvxConfigPins.gps_bus.rx) ||
#if !defined(LITE_VERSION)
            kvxConfigPins.W5500_bus.checkConflict(kvxConfigPins.gps_bus.rx) ||
            kvxConfigPins.LoRa_bus.checkConflict(kvxConfigPins.gps_bus.rx) ||
#endif
            kvxConfigPins.SDCARD_bus.checkConflict(kvxConfigPins.gps_bus.rx)) {
            // Restore the original board state after leaving the GPS app s
            // o the radio/other peripherals behave as expected
            pinMode(kvxConfigPins.gps_bus.rx, OUTPUT);
            if (kvxConfigPins.gps_bus.rx == kvxConfigPins.CC1101_bus.cs ||
                kvxConfigPins.gps_bus.rx == kvxConfigPins.NRF24_bus.cs ||
#if !defined(LITE_VERSION)
                kvxConfigPins.gps_bus.rx == kvxConfigPins.W5500_bus.cs ||
                kvxConfigPins.gps_bus.rx == kvxConfigPins.W5500_bus.cs ||
#endif
                kvxConfigPins.gps_bus.rx == kvxConfigPins.SDCARD_bus.cs) {
                // If it is conflicting to an SPI CS pin, keep it HIGH
                digitalWrite(kvxConfigPins.gps_bus.rx, HIGH);
            } else {
                // If it is conflicting with any other SPI pin, keep it LOW
                // Avoids CC1101 Jamming and nRF24 radio to keep enabled
                digitalWrite(kvxConfigPins.gps_bus.rx, LOW);
            }
        }
        rxPinReleased = false;
    }
}
