#include "gps_hw.h"
#include "root/config/configPins.h"
#include <globals.h>

namespace {

#if defined(GROVE_SCL) && defined(GROVE_SDA)
constexpr int GROVE_RX = GROVE_SCL; // Unit GPS UART_TX → host RX (Cardputer G1)
constexpr int GROVE_TX = GROVE_SDA; // Unit GPS UART_RX → host TX (Cardputer G2)
#else
constexpr int GROVE_RX = 1;
constexpr int GROVE_TX = 2;
#endif

constexpr int CAP_RX = 15; // Cap GPS_TX → host RX
constexpr int CAP_TX = 13; // Cap GPS_RX → host TX
constexpr int AT6668_BAUD = 115200;

GpsHwSource gPreferred = GpsHwSource::Auto;

void applyPins(int rx, int tx, int baud) {
    kvxConfigPins.gps_bus.rx = (gpio_num_t)rx;
    kvxConfigPins.gps_bus.tx = (gpio_num_t)tx;
    kvxConfigPins.gpsBaudrate = baud;
}

bool looksLikeLegacy9600Grove() {
    return kvxConfigPins.gpsBaudrate == 9600 &&
           ((int)kvxConfigPins.gps_bus.rx == GROVE_RX || (int)kvxConfigPins.gps_bus.rx == GROVE_TX) &&
           ((int)kvxConfigPins.gps_bus.tx == GROVE_TX || (int)kvxConfigPins.gps_bus.tx == GROVE_RX);
}

bool probePort(HardwareSerial &ser, int rx, int tx, int baud, uint32_t probeMs) {
    ser.end();
    delay(20);
    pinMode(rx, INPUT);
    ser.setRxBufferSize(2048);
    ser.begin(baud, SERIAL_8N1, rx, tx);
    delay(50);

    uint32_t t0 = millis();
    int dollars = 0;
    int nmeaish = 0;
    while (millis() - t0 < probeMs) {
        while (ser.available() > 0) {
            int c = ser.read();
            if (c < 0) break;
            if (c == '$') {
                dollars++;
                nmeaish = 1;
            } else if (nmeaish > 0 && nmeaish < 8) {
                // $GPxxx / $GNxxx / $BDxxx / $GLxxx
                if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) nmeaish++;
                else nmeaish = 0;
            }
            if (dollars >= 2 && nmeaish >= 3) return true;
        }
        delay(5);
        yield();
    }
    return dollars >= 1; // weak accept: at least one '$'
}

} // namespace

void gpsHwConfigureAt6668(HardwareSerial &ser) {
    // Known-good CASIC PCAS strings for ATGM336H / AT6668 (checksums included).
    // GGA + RMC only — full default sentence set overruns ESP32 UART during WiFi.
    ser.print("$PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0*02\r\n");
    delay(80);
    // GPS + BDS + GLONASS (common multi-GNSS mask on ATGM336H)
    ser.print("$PCAS04,7*1E\r\n");
    delay(80);
    // Drain ACK noise so TinyGPS starts clean
    uint32_t t0 = millis();
    while (millis() - t0 < 200) {
        while (ser.available()) (void)ser.read();
        delay(5);
    }
}

const char *gpsHwSourceName(GpsHwSource src) {
    switch (src) {
        case GpsHwSource::CapLora: return "Cap LoRa-1262";
        case GpsHwSource::GroveUnit: return "Unit GPS Grove";
        case GpsHwSource::Auto:
        default: return "Auto";
    }
}

GpsHwSource gpsHwPreferredSource() { return gPreferred; }

void gpsHwSetPreferredSource(GpsHwSource src) {
    gPreferred = src;
    gpsHwApplySource(src == GpsHwSource::Auto ? GpsHwSource::CapLora : src);
    if (src == GpsHwSource::Auto) {
        // Keep Cap as the stored default for Adv; autodetect still runs at session start.
#if defined(HAS_LORA_CAP)
        applyPins(CAP_RX, CAP_TX, AT6668_BAUD);
#else
        applyPins(GROVE_RX, GROVE_TX, AT6668_BAUD);
#endif
    }
    kvxConfigPins.saveFile();
}

void gpsHwApplySource(GpsHwSource src) {
    switch (src) {
        case GpsHwSource::GroveUnit:
            applyPins(GROVE_RX, GROVE_TX, AT6668_BAUD);
            break;
        case GpsHwSource::CapLora:
            applyPins(CAP_RX, CAP_TX, AT6668_BAUD);
            break;
        case GpsHwSource::Auto:
        default:
#if defined(HAS_LORA_CAP)
            applyPins(CAP_RX, CAP_TX, AT6668_BAUD);
#else
            applyPins(GROVE_RX, GROVE_TX, AT6668_BAUD);
#endif
            break;
    }
}

bool gpsHwAutodetect(HardwareSerial &ser, uint32_t probeMs) {
    // Cap first — does not steal Grove I2C (PaHub / Unit Scroll).
    if (probePort(ser, CAP_RX, CAP_TX, AT6668_BAUD, probeMs)) {
        applyPins(CAP_RX, CAP_TX, AT6668_BAUD);
        gpsHwConfigureAt6668(ser);
        Serial.println("[GPS] NMEA on Cap LoRa-1262 (RX15/TX13 @ 115200)");
        return true;
    }

    if (probePort(ser, GROVE_RX, GROVE_TX, AT6668_BAUD, probeMs)) {
        applyPins(GROVE_RX, GROVE_TX, AT6668_BAUD);
        gpsHwConfigureAt6668(ser);
        Serial.println("[GPS] NMEA on Grove Unit GPS (RX1/TX2 @ 115200)");
        return true;
    }

    // Legacy Grove modules at 9600
    if (probePort(ser, GROVE_RX, GROVE_TX, 9600, probeMs)) {
        applyPins(GROVE_RX, GROVE_TX, 9600);
        gpsHwConfigureAt6668(ser);
        Serial.println("[GPS] NMEA on Grove @ 9600");
        return true;
    }

#if defined(HAS_LORA_CAP)
    applyPins(CAP_RX, CAP_TX, AT6668_BAUD);
    ser.end();
    pinMode(CAP_RX, INPUT);
    ser.setRxBufferSize(2048);
    ser.begin(AT6668_BAUD, SERIAL_8N1, CAP_RX, CAP_TX);
    gpsHwConfigureAt6668(ser);
    Serial.println("[GPS] No NMEA yet — Cap LoRa defaults (RX15/TX13 @ 115200)");
#else
    applyPins(GROVE_RX, GROVE_TX, AT6668_BAUD);
    ser.end();
    pinMode(GROVE_RX, INPUT);
    ser.setRxBufferSize(2048);
    ser.begin(AT6668_BAUD, SERIAL_8N1, GROVE_RX, GROVE_TX);
    gpsHwConfigureAt6668(ser);
    Serial.println("[GPS] No NMEA yet — Grove Unit defaults (RX1/TX2 @ 115200)");
#endif
    return false;
}

GpsHwSource gpsHwCurrentSource() {
    const int rx = (int)kvxConfigPins.gps_bus.rx;
    const int tx = (int)kvxConfigPins.gps_bus.tx;
    if (rx == CAP_RX && tx == CAP_TX) return GpsHwSource::CapLora;
    if (rx == GROVE_RX && tx == GROVE_TX) return GpsHwSource::GroveUnit;
    return GpsHwSource::Auto;
}

GpsHwInfo gpsHwPrepare(HardwareSerial &ser) {
    // Migrate stale 9600 Grove defaults left by older firmware / pin files.
    if (looksLikeLegacy9600Grove() || kvxConfigPins.gpsBaudrate == 9600) {
#if defined(HAS_LORA_CAP)
        applyPins(CAP_RX, CAP_TX, AT6668_BAUD);
#else
        applyPins(GROVE_RX, GROVE_TX, AT6668_BAUD);
#endif
    }

    GpsHwInfo info;
    if (gPreferred == GpsHwSource::CapLora) {
        gpsHwApplySource(GpsHwSource::CapLora);
        ser.end();
        pinMode(CAP_RX, INPUT);
        ser.setRxBufferSize(2048);
        ser.begin(AT6668_BAUD, SERIAL_8N1, CAP_RX, CAP_TX);
        gpsHwConfigureAt6668(ser);
        info.source = GpsHwSource::CapLora;
        info.rx = CAP_RX;
        info.tx = CAP_TX;
        info.baud = AT6668_BAUD;
        info.label = gpsHwSourceName(info.source);
        return info;
    }
    if (gPreferred == GpsHwSource::GroveUnit) {
        gpsHwApplySource(GpsHwSource::GroveUnit);
        ser.end();
        pinMode(GROVE_RX, INPUT);
        ser.setRxBufferSize(2048);
        ser.begin(AT6668_BAUD, SERIAL_8N1, GROVE_RX, GROVE_TX);
        gpsHwConfigureAt6668(ser);
        info.source = GpsHwSource::GroveUnit;
        info.rx = GROVE_RX;
        info.tx = GROVE_TX;
        info.baud = AT6668_BAUD;
        info.label = gpsHwSourceName(info.source);
        return info;
    }

    bool ok = gpsHwAutodetect(ser, 1500);
    info.source = gpsHwCurrentSource();
    info.rx = (int)kvxConfigPins.gps_bus.rx;
    info.tx = (int)kvxConfigPins.gps_bus.tx;
    info.baud = kvxConfigPins.gpsBaudrate;
    info.label = ok ? gpsHwSourceName(info.source) : "waiting";
    return info;
}
