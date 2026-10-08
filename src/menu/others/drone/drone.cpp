#include "drone.h"

#ifndef LITE_VERSION

#include "menu/others/audio.h"
#include "opendroneid.h"
#include "odid_wifi.h"
#include "root/net/wifi_common.h"
#include "root/hal/ble/ble_backend.h"
#include "root/ui/display.h"
#include "root/ui/scanner_list.h"
#include "root/input/mykeyboard.h"

#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <globals.h>
#include <nvs_flash.h>
#include <string.h>
#include <vector>

extern "C" {
int odid_message_process_pack(ODID_UAS_Data *UAS_Data, uint8_t *pack, size_t buflen);
int odid_wifi_receive_message_pack_nan_action_frame(ODID_UAS_Data *UAS_Data, char *mac, uint8_t *buf,
                                                    size_t buf_size);
ODID_messagetype_t decodeOpenDroneID(ODID_UAS_Data *uas_data, uint8_t *msg_data);
}

namespace {

constexpr int kMaxUavs = 24;
constexpr uint16_t kWifiDwellMs = 120;
constexpr int kPktQueue = 8;
constexpr int kMaxPktLen = 512;

struct QueuedPkt {
    int len;
    int rssi;
    uint8_t data[kMaxPktLen];
};

struct UavHit {
    uint8_t mac[6] = {};
    char id[ODID_ID_SIZE + 1] = {};
    char opId[ODID_ID_SIZE + 1] = {};
    int rssi = 0;
    double lat = 0;
    double lon = 0;
    double opLat = 0;
    double opLon = 0;
    int altMsl = 0;
    int heightAgl = 0;
    int speed = 0;
    int heading = 0;
    bool haveLoc = false;
    bool haveOpLoc = false;
    unsigned long lastSeen = 0;
    bool isNew = false;
};

UavHit gUavs[kMaxUavs];
int gUavCount = 0;
portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool gWifiActive = false;

QueuedPkt gQueue[kPktQueue];
volatile int gQHead = 0;
volatile int gQTail = 0;

// Large ODID structs — keep off the task stack.
static ODID_UAS_Data gParseUas;

String macStr(const uint8_t *mac) {
    char b[18];
    snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(b);
}

UavHit *findOrAlloc(const uint8_t *mac) {
    for (int i = 0; i < gUavCount; i++) {
        if (memcmp(gUavs[i].mac, mac, 6) == 0) return &gUavs[i];
    }
    auto resetHit = [](UavHit *u, const uint8_t *m) {
        memset(u, 0, sizeof(*u));
        memcpy(u->mac, m, 6);
        u->isNew = true;
    };
    if (gUavCount < kMaxUavs) {
        UavHit *u = &gUavs[gUavCount++];
        resetHit(u, mac);
        return u;
    }
    int oldest = 0;
    for (int i = 1; i < kMaxUavs; i++) {
        if (gUavs[i].lastSeen < gUavs[oldest].lastSeen) oldest = i;
    }
    UavHit *u = &gUavs[oldest];
    resetHit(u, mac);
    return u;
}

void applyUas(UavHit *u, const ODID_UAS_Data &uas, int rssi) {
    u->rssi = rssi;
    u->lastSeen = millis();
    if (uas.BasicIDValid[0]) {
        memcpy(u->id, uas.BasicID[0].UASID, ODID_ID_SIZE);
        u->id[ODID_ID_SIZE] = 0;
        // trim trailing spaces
        for (int i = ODID_ID_SIZE - 1; i >= 0 && (u->id[i] == ' ' || u->id[i] == 0); i--) u->id[i] = 0;
    }
    if (uas.LocationValid) {
        u->lat = uas.Location.Latitude;
        u->lon = uas.Location.Longitude;
        u->altMsl = (int)uas.Location.AltitudeGeo;
        u->heightAgl = (int)uas.Location.Height;
        u->speed = (int)uas.Location.SpeedHorizontal;
        u->heading = (int)uas.Location.Direction;
        u->haveLoc = true;
    }
    if (uas.SystemValid) {
        u->opLat = uas.System.OperatorLatitude;
        u->opLon = uas.System.OperatorLongitude;
        u->haveOpLoc = true;
    }
    if (uas.OperatorIDValid) {
        memcpy(u->opId, uas.OperatorID.OperatorId, ODID_ID_SIZE);
        u->opId[ODID_ID_SIZE] = 0;
        for (int i = ODID_ID_SIZE - 1; i >= 0 && (u->opId[i] == ' ' || u->opId[i] == 0); i--) u->opId[i] = 0;
    }
}

void ingestUas(const uint8_t *mac, int rssi, const ODID_UAS_Data &uas) {
    bool useful = uas.BasicIDValid[0] || uas.LocationValid || uas.SystemValid || uas.OperatorIDValid;
    if (!useful) return;
    portENTER_CRITICAL(&gMux);
    UavHit *u = findOrAlloc(mac);
    applyUas(u, uas, rssi);
    portEXIT_CRITICAL(&gMux);
}

void processWifiPayload(const uint8_t *payload, int length, int rssi) {
    if (length < 24) return;
    static const uint8_t nan_dest[6] = {0x51, 0x6f, 0x9a, 0x01, 0x00, 0x00};
    char macBuf[6];

    if (memcmp(nan_dest, &payload[4], 6) == 0) {
        memset(&gParseUas, 0, sizeof(gParseUas));
        if (odid_wifi_receive_message_pack_nan_action_frame(&gParseUas, macBuf, (uint8_t *)payload,
                                                            (size_t)length) == 0) {
            ingestUas((uint8_t *)macBuf, rssi, gParseUas);
        }
        return;
    }

    if (payload[0] == 0x80) {
        int offset = 36;
        while (offset + 2 < length) {
            int typ = payload[offset];
            int len = payload[offset + 1];
            if (offset + 2 + len > length) break;
            if (typ == 0xdd && len >= 5) {
                uint8_t o0 = payload[offset + 2], o1 = payload[offset + 3], o2 = payload[offset + 4];
                bool odidOui = (o0 == 0x90 && o1 == 0x3a && o2 == 0xe6) ||
                               (o0 == 0xfa && o1 == 0x0b && o2 == 0xbc);
                if (odidOui) {
                    int j = offset + 7;
                    if (j < length) {
                        memset(&gParseUas, 0, sizeof(gParseUas));
                        if (odid_message_process_pack(&gParseUas, (uint8_t *)&payload[j],
                                                      (size_t)(length - j)) >= 0) {
                            ingestUas(&payload[10], rssi, gParseUas);
                        }
                    }
                }
            }
            offset += len + 2;
        }
    }
}

void drainWifiQueue() {
    while (true) {
        QueuedPkt pkt;
        portENTER_CRITICAL(&gMux);
        if (gQHead == gQTail) {
            portEXIT_CRITICAL(&gMux);
            break;
        }
        pkt = gQueue[gQHead];
        gQHead = (gQHead + 1) % kPktQueue;
        portEXIT_CRITICAL(&gMux);
        processWifiPayload(pkt.data, pkt.len, pkt.rssi);
    }
}

void IRAM_ATTR wifiRxCb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (!gWifiActive) return;
    if (type != WIFI_PKT_MGMT) return;
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    if (!pkt) return;
    int length = pkt->rx_ctrl.sig_len;
    if (length < 24) return;
    if (length > kMaxPktLen) length = kMaxPktLen;

    const uint8_t *payload = pkt->payload;
    static const uint8_t nan_dest[6] = {0x51, 0x6f, 0x9a, 0x01, 0x00, 0x00};
    bool interesting = (memcmp(nan_dest, &payload[4], 6) == 0) || (payload[0] == 0x80);
    if (!interesting) return;

    portENTER_CRITICAL(&gMux);
    int next = (gQTail + 1) % kPktQueue;
    if (next != gQHead) {
        gQueue[gQTail].len = length;
        gQueue[gQTail].rssi = pkt->rx_ctrl.rssi;
        memcpy(gQueue[gQTail].data, payload, length);
        gQTail = next;
    }
    portEXIT_CRITICAL(&gMux);
}

bool wifiStartScan() {
    ensureWifiPlatform();
    nvs_flash_init();

    // Tear down Arduino WiFi / prior sessions cleanly before raw driver use.
    if (WiFi.getMode() != WIFI_MODE_NULL || wifiConnected) {
        wifiDisconnect();
        delay(100);
    }
    WiFi.mode(WIFI_OFF);
    delay(50);
    esp_wifi_stop();
    esp_wifi_deinit();
    delay(100);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t e = esp_wifi_init(&cfg);
    if (e != ESP_OK && e != ESP_ERR_WIFI_INIT_STATE) {
        Serial.printf("[DroneID] wifi_init: %s\n", esp_err_to_name(e));
        return false;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    e = esp_wifi_set_mode(WIFI_MODE_STA);
    if (e != ESP_OK) {
        Serial.printf("[DroneID] set_mode: %s\n", esp_err_to_name(e));
        return false;
    }
    e = esp_wifi_start();
    if (e != ESP_OK && e != ESP_ERR_WIFI_CONN) {
        Serial.printf("[DroneID] wifi_start: %s\n", esp_err_to_name(e));
        return false;
    }
    esp_wifi_disconnect();
    esp_wifi_set_promiscuous(true);
    wifi_promiscuous_filter_t filt = {};
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(wifiRxCb);
    gWifiActive = true;
    return true;
}

void wifiStopScan() {
    gWifiActive = false;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    esp_wifi_stop();
    delay(50);
    esp_wifi_deinit();
    delay(80);
    wifiDisconnect();
    WiFi.mode(WIFI_OFF);
    delay(50);
}

void wifiHopSlice() {
    static const uint8_t chans[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    for (uint8_t ch : chans) {
        if (returnToMenu || forceHome) return;
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        delay(kWifiDwellMs);
        drainWifiQueue();
    }
}

void parseBleOdId(const ScannerAdvSnap &adv) {
    if (adv.payload.size() < 8) return;
    const uint8_t *payload = adv.payload.data();
    int len = (int)adv.payload.size();
    for (int i = 0; i + 6 < len; i++) {
        if (payload[i] == 0x16 && payload[i + 1] == 0xFA && payload[i + 2] == 0xFF && payload[i + 3] == 0x0D) {
            uint8_t *odid = (uint8_t *)&payload[i + 5];
            int odidLen = len - (i + 5);
            if (odidLen < 25) return;
            memset(&gParseUas, 0, sizeof(gParseUas));
            if ((odid[0] & 0xF0) == (ODID_MESSAGETYPE_PACKED << 4)) {
                odid_message_process_pack(&gParseUas, odid, (size_t)odidLen);
            } else {
                decodeOpenDroneID(&gParseUas, odid);
            }
            uint8_t mac[6] = {};
            unsigned a, b, c, d, e, f;
            if (sscanf(adv.mac.c_str(), "%02X:%02X:%02X:%02X:%02X:%02X", &a, &b, &c, &d, &e, &f) == 6) {
                mac[0] = a;
                mac[1] = b;
                mac[2] = c;
                mac[3] = d;
                mac[4] = e;
                mac[5] = f;
            }
            ingestUas(mac, adv.rssi, gParseUas);
            return;
        }
    }
}

void bleSlice() {
    NimBLEScan *scan = scannerBleStart();
    if (!scan) return;
    unsigned long t0 = millis();
    while (millis() - t0 < 2000 && !returnToMenu && !forceHome) {
        scannerBleKeepAlive(scan);
        auto inbox = scannerBleTakeInbox(scan);
        for (const auto &adv : inbox) parseBleOdId(adv);
        delay(50);
    }
    scannerBleTeardown(true);
    delay(100);
}

std::vector<String> rowLabels() {
    std::vector<String> rows;
    portENTER_CRITICAL(&gMux);
    int n = gUavCount;
    UavHit snap[kMaxUavs];
    for (int i = 0; i < n; i++) snap[i] = gUavs[i];
    portEXIT_CRITICAL(&gMux);
    for (int i = 0; i < n; i++) {
        String id = snap[i].id[0] ? String(snap[i].id) : macStr(snap[i].mac);
        if (id.length() > 18) id = id.substring(0, 18);
        rows.push_back(id + "  " + String(snap[i].rssi) + "dBm");
    }
    return rows;
}

void showDetail(int idx) {
    UavHit snap;
    portENTER_CRITICAL(&gMux);
    if (idx < 0 || idx >= gUavCount) {
        portEXIT_CRITICAL(&gMux);
        return;
    }
    snap = gUavs[idx];
    portEXIT_CRITICAL(&gMux);

    std::vector<ScannerDetailField> fields;
    fields.push_back({"ID", snap.id[0] ? String(snap.id) : String("-")});
    fields.push_back({"MAC", macStr(snap.mac)});
    fields.push_back({"RSSI", String(snap.rssi) + " dBm"});
    if (snap.haveLoc) {
        fields.push_back({"Lat", String(snap.lat, 6)});
        fields.push_back({"Lon", String(snap.lon, 6)});
        fields.push_back({"Alt MSL", String(snap.altMsl) + " m"});
        fields.push_back({"Height", String(snap.heightAgl) + " m"});
        fields.push_back({"Speed", String(snap.speed)});
        fields.push_back({"Heading", String(snap.heading)});
    }
    if (snap.haveOpLoc) {
        fields.push_back({"Op Lat", String(snap.opLat, 6)});
        fields.push_back({"Op Lon", String(snap.opLon, 6)});
    }
    if (snap.opId[0]) fields.push_back({"Operator", String(snap.opId)});

    scannerListShowDetail("Drone ID", fields, nullptr);
}

void beepNew() {
#if defined(HAS_NS4168_SPKR) || defined(BUZZ_PIN)
    if (kvxConfig.soundEnabled) _tone(1000, 80);
#endif
}

void runWifiScanLoop() {
    if (!tab5RadioLater("Drone ID WiFi")) return;

    ScannerListState list;
    scannerListBegin(list, "Drone ID WiFi", "scanning…");

    if (!wifiStartScan()) {
        displayError("WiFi start failed", true);
        scannerListEnd();
        return;
    }

    unsigned long lastBeepCheck = 0;
    while (!returnToMenu && !forceHome) {
        wifiHopSlice();
        if (returnToMenu || forceHome) break;

        if (millis() - lastBeepCheck > 200) {
            lastBeepCheck = millis();
            bool doBeep = false;
            portENTER_CRITICAL(&gMux);
            for (int i = 0; i < gUavCount; i++) {
                if (gUavs[i].isNew) {
                    gUavs[i].isNew = false;
                    doBeep = true;
                }
            }
            portEXIT_CRITICAL(&gMux);
            if (doBeep) beepNew();
        }

        scannerListSetRows(list, rowLabels());
        String st = String(gUavCount) + " drone(s)";
        scannerListSetStatus(list, st.c_str());
        scannerListRefresh(list);

        ScannerListResult r = scannerListPoll(list);
        if (r == SCANNER_LIST_EXIT) break;
        if (r == SCANNER_LIST_DETAIL) {
            wifiStopScan();
            showDetail(list.cursor);
            if (!wifiStartScan()) break;
            scannerListBegin(list, "Drone ID WiFi", "scanning…");
        }
    }

    wifiStopScan();
    scannerListEnd();
}

void runBleScanLoop() {
    if (!bleNimbleProfileOrExplain("Drone ID BLE")) return;

    ScannerListState list;
    scannerListBegin(list, "Drone ID BLE", "scanning…");

    unsigned long lastBeepCheck = 0;
    while (!returnToMenu && !forceHome) {
        bleSlice();
        if (returnToMenu || forceHome) break;

        if (millis() - lastBeepCheck > 200) {
            lastBeepCheck = millis();
            bool doBeep = false;
            portENTER_CRITICAL(&gMux);
            for (int i = 0; i < gUavCount; i++) {
                if (gUavs[i].isNew) {
                    gUavs[i].isNew = false;
                    doBeep = true;
                }
            }
            portEXIT_CRITICAL(&gMux);
            if (doBeep) beepNew();
        }

        scannerListSetRows(list, rowLabels());
        String st = String(gUavCount) + " drone(s)";
        scannerListSetStatus(list, st.c_str());
        scannerListRefresh(list);

        ScannerListResult r = scannerListPoll(list);
        if (r == SCANNER_LIST_EXIT) break;
        if (r == SCANNER_LIST_DETAIL) {
            showDetail(list.cursor);
            scannerListBegin(list, "Drone ID BLE", "scanning…");
        }
    }

    scannerBleTeardown(true);
    scannerListEnd();
}

} // namespace

void droneIdMenu() {
    gUavCount = 0;
    memset(gUavs, 0, sizeof(gUavs));
    gQHead = gQTail = 0;

    while (!returnToMenu && !forceHome) {
        std::vector<Option> opts = {
            {"WiFi scan", runWifiScanLoop},
            {"BLE scan", runBleScanLoop},
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "Drone ID");
        if (sel < 0 || sel == (int)opts.size() - 1 || forceHome) return;
    }
}

#else

void droneIdMenu() {}

#endif
