#include "pc_connect.h"

#if !defined(LITE_VERSION)
#include "menu/rf/protocols/rf_decoder.h"
#include "menu/rf/rf_scan.h"
#include "menu/rf/rf_send.h"
#include "menu/rf/rf_utils.h"
#include "root/app/type_convertion.h"
#include <ArduinoJson.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>

namespace {

bool g_rxActive = false;
bool g_rssiActive = false;
bool g_raw = false;
float g_mhz = 433.92f;
RfRxSession *g_rx = nullptr;
RfCodes g_last;
bool g_haveLast = false;
int g_rssiIdx = 0;
uint32_t g_rssiLastMs = 0;

String truncateData(const String &s, size_t maxLen = 400) {
    if (s.length() <= maxLen) return s;
    return s.substring(0, maxLen);
}

void emitRf(const RfCodes &c, float mhz) {
    JsonDocument doc;
    doc["evt"] = "rf";
    doc["mhz"] = mhz;
    doc["protocol"] = c.protocol;
    doc["bits"] = c.Bit;
    doc["te"] = c.te;
    doc["preset"] = c.preset;
    if (c.protocol == "RAW") {
        doc["data"] = truncateData(c.data);
    } else {
        char hexString[64] = {0};
        decimalToHexString(c.key, hexString);
        doc["key"] = hexString;
        if (c.fix != 0) {
            doc["mf_name"] = c.mf_name;
            char tmp[32] = {0};
            decimalToHexString(c.serial, tmp);
            doc["serial"] = tmp;
            doc["btn"] = c.btn;
            doc["cnt"] = c.cnt;
        }
    }
    String out;
    serializeJson(doc, out);
    pcConnectEmitJson(out);
}

} // namespace

bool pcConnectRfRxActive() { return g_rxActive; }
bool pcConnectRfRssiActive() { return g_rssiActive; }

bool pcConnectRfRxStart(float mhz, bool raw) {
    pcConnectStopRadio();
    if (mhz <= 0) mhz = kvxConfigPins.rfFreq;
    g_mhz = mhz;
    g_raw = raw;
    if (!initRfModule("rx", mhz)) return false;
    g_rx = new RfRxSession();
    if (!g_rx || !g_rx->begin()) {
        delete g_rx;
        g_rx = nullptr;
        deinitRfModule();
        return false;
    }
    g_rxActive = true;
    pcConnectSetStatus(PcRadio::Rf, raw ? "rf.rx.raw" : "rf.rx", 0);
    return true;
}

void pcConnectRfRxStop() {
    if (!g_rxActive) return;
    if (g_rx) {
        g_rx->end();
        delete g_rx;
        g_rx = nullptr;
    }
    deinitRfModule();
    g_rxActive = false;
}

void pcConnectRfRxTick() {
    if (!g_rxActive || !g_rx) return;
    std::vector<int> durations;
    if (!g_rx->poll(durations)) return;

    RfCodes received;
    bool decoded = (!g_raw) && (rf_try_keeloq(durations, received) || rf_decode_ook(durations, received));

    String _data;
    bool hasCrc = false;
    uint64_t crc = 0;
    std::vector<int> indexed;
    int rawBits = 0, rawTe = 0;
    int transitions = rf_build_raw(durations, _data, hasCrc, crc, indexed, rawBits, rawTe);

    if (decoded) {
        received.frequency = (uint32_t)(g_mhz * 1000000.0f);
        received.data = _data;
        g_last = received;
        g_haveLast = true;
        emitRf(received, g_mhz);
    } else if (g_raw && transitions > 20) {
        received.frequency = (uint32_t)(g_mhz * 1000000.0f);
        received.protocol = "RAW";
        received.preset = "Ook270Async";
        received.te = rawTe;
        received.data = _data;
        received.Bit = rawBits;
        g_last = received;
        g_haveLast = true;
        emitRf(received, g_mhz);
    }
}

bool pcConnectRfRssiStart() {
    pcConnectStopRadio();
    if (kvxConfigPins.rfModule != CC1101_SPI_MODULE) return false;
    float startMhz = kvxConfigPins.rfFreq;
    if (!initRfModule("rx", startMhz)) return false;
    g_rssiIdx = range_limits[kvxConfigPins.rfScanRange][0];
    g_rssiLastMs = 0;
    g_rssiActive = true;
    pcConnectSetStatus(PcRadio::Rf, "rf.rssi", 0);
    return true;
}

void pcConnectRfRssiStop() {
    if (!g_rssiActive) return;
    deinitRfModule();
    g_rssiActive = false;
}

void pcConnectRfRssiTick() {
    if (!g_rssiActive) return;
    uint32_t now = millis();
    if (now - g_rssiLastMs < 40) return;
    g_rssiLastMs = now;

    int lo = range_limits[kvxConfigPins.rfScanRange][0];
    int hi = range_limits[kvxConfigPins.rfScanRange][1];
    if (g_rssiIdx < lo || g_rssiIdx > hi) g_rssiIdx = lo;

    float mhz = subghz_frequency_list[g_rssiIdx];
    setMHZ(mhz);
    int rssi = ELECHOUSE_cc1101.getRssi();

    JsonDocument doc;
    doc["evt"] = "rf.rssi";
    doc["mhz"] = mhz;
    doc["rssi"] = rssi;
    String out;
    serializeJson(doc, out);
    pcConnectEmitJson(out);

    g_rssiIdx++;
    if (g_rssiIdx > hi) g_rssiIdx = lo;
}

bool pcConnectRfTxLast() {
    if (!g_haveLast) return false;
    bool wasRx = g_rxActive;
    bool wasRaw = g_raw;
    float mhz = g_mhz;
    if (wasRx) pcConnectRfRxStop();
    else if (g_rssiActive) pcConnectRfRssiStop();
    else if (pcConnectRadio() != PcRadio::Idle) pcConnectStopRadio();

    sendRfCommand(g_last, true);

    if (wasRx) pcConnectRfRxStart(mhz, wasRaw);
    return true;
}

#endif
