#include "root/hal/ble/ble_backend.h"

#if defined(KVX_BLE_BACKEND_HOSTED)

#include "root/hal/radio_mem.h"
#include "root/input/mykeyboard.h"
#include "root/ui/display.h"
#include "root/ui/scanner_list.h"
#include "esp32-hal-hosted.h"

#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_hs_adv.h>
#include <services/gap/ble_svc_gap.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <store/config/ble_store_config.h>

#include <cstring>
#include <vector>

extern "C" void ble_store_config_init(void);

constexpr int kMaxHits = 48;

namespace {

portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
BleScanResult gHits[kMaxHits];
int gHitCount = 0;
volatile bool gSynced = false;
bool gInited = false;
bool gScanning = false;
bool gAdvertising = false;

void formatAddr(const ble_addr_t &addr, char *out, size_t outLen) {
    // NimBLE stores the address little-endian.
    snprintf(
        out,
        outLen,
        "%02X:%02X:%02X:%02X:%02X:%02X",
        addr.val[5],
        addr.val[4],
        addr.val[3],
        addr.val[2],
        addr.val[1],
        addr.val[0]
    );
}

void upsertHit(const ble_gap_disc_desc *disc) {
    if (!disc) return;
    BleScanResult hit;
    memset(&hit, 0, sizeof(hit));
    formatAddr(disc->addr, hit.addr, sizeof(hit.addr));
    hit.rssi = disc->rssi;
    hit.addrType = disc->addr.type;

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    if (disc->data && disc->length_data &&
        ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) == 0) {
        if (fields.name && fields.name_len) {
            size_t n = fields.name_len;
            if (n > sizeof(hit.name) - 1) n = sizeof(hit.name) - 1;
            memcpy(hit.name, fields.name, n);
            hit.name[n] = 0;
        }
        if (fields.mfg_data && fields.mfg_data_len) {
            size_t n = fields.mfg_data_len;
            if (n > sizeof(hit.mfg)) n = sizeof(hit.mfg);
            memcpy(hit.mfg, fields.mfg_data, n);
            hit.mfgLen = (uint8_t)n;
        }
    }

    portENTER_CRITICAL(&gMux);
    int slot = -1;
    for (int i = 0; i < gHitCount; i++) {
        if (strcmp(gHits[i].addr, hit.addr) == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (gHitCount < kMaxHits) slot = gHitCount++;
    }
    if (slot >= 0) {
        if (hit.name[0] == 0 && gHits[slot].name[0]) {
            memcpy(hit.name, gHits[slot].name, sizeof(hit.name));
        }
        if (hit.mfgLen == 0 && gHits[slot].mfgLen) {
            memcpy(hit.mfg, gHits[slot].mfg, gHits[slot].mfgLen);
            hit.mfgLen = gHits[slot].mfgLen;
        }
        gHits[slot] = hit;
    }
    portEXIT_CRITICAL(&gMux);
}

int gapEvent(struct ble_gap_event *event, void *) {
    if (!event) return 0;
    if (event->type == BLE_GAP_EVENT_DISC) upsertHit(&event->disc);
    return 0;
}

void onReset(int) { gSynced = false; }

void onSync(void) { gSynced = true; }

void hostTask(void *) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

class BleBackendHostedC6 : public BleBackend {
public:
    const char *name() const override { return "hosted-c6"; }
    bool isSupported() const override { return true; }
    uint32_t features() const override { return BLE_FEAT_SCAN | BLE_FEAT_ADV; }
    const char *unsupportedReason(BleFeature f) const override {
        if (f == BLE_FEAT_HID) return "needs hosted C6 BLE GATT (HID)";
        if (f == BLE_FEAT_GATT_CLIENT || f == BLE_FEAT_GATT_SERVER) return "needs hosted C6 BLE GATT";
        if (f == BLE_FEAT_SCAN) return "needs hosted C6 BLE GAP scan";
        if (f == BLE_FEAT_ADV) return "needs hosted C6 BLE GAP advertise";
        return "needs hosted C6 BLE";
    }

    bool init() override {
        if (gInited && gSynced) return true;
        uiRamEnterHeavy();
        if (!hostedInitBLE()) {
            Serial.println("[BLE] hostedInitBLE failed");
            uiRamLeaveHeavy();
            return false;
        }
        if (!gInited) {
            esp_err_t err = nimble_port_init();
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                Serial.printf("[BLE] nimble_port_init %d\n", (int)err);
                uiRamLeaveHeavy();
                return false;
            }
            ble_hs_cfg.reset_cb = onReset;
            ble_hs_cfg.sync_cb = onSync;
            ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
            ble_hs_cfg.sm_bonding = 0;
            ble_hs_cfg.sm_mitm = 0;
            ble_hs_cfg.sm_sc = 1;
            ble_svc_gap_device_name_set("kvxputer");
            ble_store_config_init();
            nimble_port_freertos_init(hostTask);
            gInited = true;
        }
        for (int i = 0; i < 300 && !gSynced; i++) vTaskDelay(pdMS_TO_TICKS(10));
        if (!gSynced) {
            Serial.println("[BLE] hosted C6 sync timeout");
            uiRamLeaveHeavy();
            return false;
        }
        return true;
    }

    void deinit() override {
        stopScan();
        stopAdvertise();
        if (gInited) {
        nimble_port_stop();
        nimble_port_deinit();
        gInited = false;
        gSynced = false;
        }
        // Leave the ESP-Hosted SDIO transport up. WiFi STA shares it with BLE HCI.
        uiRamLeaveHeavy();
    }

    bool startScan() override {
        if (!init()) return false;
        stopAdvertise();
        portENTER_CRITICAL(&gMux);
        gHitCount = 0;
        portEXIT_CRITICAL(&gMux);
        struct ble_gap_disc_params params;
        memset(&params, 0, sizeof(params));
        params.itvl = 160;
        params.window = 80;
        params.filter_duplicates = 0;
        params.passive = 0;
        int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &params, gapEvent, nullptr);
        if (rc != 0 && rc != BLE_HS_EALREADY) {
            Serial.printf("[BLE] ble_gap_disc %d\n", rc);
            return false;
        }
        gScanning = true;
        return true;
    }

    void stopScan() override {
        if (!gScanning && !ble_gap_disc_active()) return;
        ble_gap_disc_cancel();
        gScanning = false;
    }

    int copyScan(BleScanResult *out, int max) override {
        if (!out || max <= 0) return 0;
        portENTER_CRITICAL(&gMux);
        int n = gHitCount;
        if (n > max) n = max;
        if (n > 0) memcpy(out, gHits, (size_t)n * sizeof(BleScanResult));
        portEXIT_CRITICAL(&gMux);
        return n;
    }

    bool advertiseRaw(const char *name, const uint8_t *mfg, size_t mfgLen) {
        if (!init()) return false;
        stopScan();
        if (gAdvertising) {
            ble_gap_adv_stop();
            gAdvertising = false;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (name && name[0]) ble_svc_gap_device_name_set(name);
        struct ble_hs_adv_fields fields;
        memset(&fields, 0, sizeof(fields));
        fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
        if (name && name[0]) {
            fields.name = (const uint8_t *)name;
            fields.name_len = (uint8_t)strlen(name);
            if (fields.name_len > 20) fields.name_len = 20;
            fields.name_is_complete = 1;
        }
        if (mfg && mfgLen) {
            fields.mfg_data = mfg;
            fields.mfg_data_len = (uint8_t)(mfgLen > 29 ? 29 : mfgLen);
        }
        int rc = ble_gap_adv_set_fields(&fields);
        if (rc != 0) {
            Serial.printf("[BLE] adv_set_fields %d\n", rc);
            return false;
        }
        struct ble_gap_adv_params adv;
        memset(&adv, 0, sizeof(adv));
        adv.conn_mode = BLE_GAP_CONN_MODE_NON;
        adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
        rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER, &adv, gapEvent, nullptr);
        if (rc != 0 && rc != BLE_HS_EALREADY) {
            Serial.printf("[BLE] adv_start %d\n", rc);
            return false;
        }
        gAdvertising = true;
        return true;
    }

    bool advertiseName(const char *name) override { return advertiseRaw(name, nullptr, 0); }

    bool advertiseMfg(const char *name, const uint8_t *mfg, size_t mfgLen) override {
        return advertiseRaw(name, mfg, mfgLen);
    }

    void stopAdvertise() override {
        if (!gAdvertising) return;
        ble_gap_adv_stop();
        gAdvertising = false;
    }
};

BleBackendHostedC6 gBackend;

} // namespace

BleBackend &bleBackend() { return gBackend; }

void bleHostedScanApp() {
    if (!bleBackend().startScan()) {
        displayError("BLE Scan\nneeds hosted C6 BLE GAP", true);
        bleBackend().deinit();
        return;
    }

    ScannerListState ui;
    scannerListBegin(ui, "BLE Scan", "hosted C6");
    std::vector<BleScanResult> shown;
    unsigned long last = 0;

    while (true) {
        if (millis() - last > 400) {
            last = millis();
            BleScanResult tmp[kMaxHits];
            int n = bleBackend().copyScan(tmp, kMaxHits);
            shown.clear();
            std::vector<String> rows;
            for (int i = 0; i < n; i++) {
                shown.push_back(tmp[i]);
                String row = tmp[i].name[0] ? String(tmp[i].name) : String(tmp[i].addr);
                row += "  " + String((int)tmp[i].rssi);
                rows.push_back(row);
            }
            scannerListSetRows(ui, rows);
            scannerListSetStatus(ui, (String(n) + " hosted C6").c_str());
        }
        ScannerListResult r = scannerListPoll(ui);
        if (r == SCANNER_LIST_EXIT) break;
        if (r == SCANNER_LIST_DETAIL && ui.cursor >= 0 && ui.cursor < (int)shown.size()) {
            const BleScanResult &h = shown[ui.cursor];
            std::vector<ScannerDetailField> f;
            f.push_back({"Name", h.name[0] ? h.name : "(none)"});
            f.push_back({"Addr", h.addr});
            f.push_back({"RSSI", String((int)h.rssi) + " dBm"});
            if (h.mfgLen >= 2) {
                char id[8];
                snprintf(id, sizeof(id), "%02X%02X", h.mfg[1], h.mfg[0]);
                f.push_back({"Mfg", id});
            }
            scannerListShowDetail("BLE Scan", f, nullptr);
            scannerListBegin(ui, "BLE Scan", "hosted C6");
        }
        delay(20);
    }

    scannerListEnd();
    bleBackend().stopScan();
    bleBackend().deinit();
}

#endif
