#include "ble_common.h"
#include "root/input/mykeyboard.h"
#include "root/hal/radio_mem.h"
#include "root/app/ram_profile.h"
#include "root/app/utils.h"
#include "root/net/wifi_common.h"
#include "root/ui/scanner_list.h"
#include "esp_mac.h"
#include "menu/others/badusb_ble/ducky_typer.h"
#if !defined(LITE_VERSION)
#include "BLE_Suite.h"
#endif
#include <set>
#include <vector>

#define SERVICE_UUID "1bc68b2a-f3e3-11e9-81b4-2a2ae2dbcce4"
#define CHARACTERISTIC_RX_UUID "1bc68da0-f3e3-11e9-81b4-2a2ae2dbcce4"
#define CHARACTERISTIC_TX_UUID "1bc68efe-f3e3-11e9-81b4-2a2ae2dbcce4"

BLEScan *pBLEScan = nullptr;
int scanTime = SCANTIME;

bool bleNotifyRetry(NimBLECharacteristic *chr, const uint8_t *value, size_t length, uint8_t retries) {
    if (chr == nullptr) return false;
    if (chr->notify(value, length)) return true;
    for (uint8_t i = 0; i < retries; i++) {
        vTaskDelay(1);
        if (chr->notify(value, length)) return true;
    }
    return false;
}

bool bleNotifyRetry(NimBLECharacteristic *chr, uint8_t retries) {
    if (chr == nullptr) return false;
    if (chr->notify()) return true;
    for (uint8_t i = 0; i < retries; i++) {
        vTaskDelay(1);
        if (chr->notify()) return true;
    }
    return false;
}

#define ENDIAN_CHANGE_U16(x) ((((x) & 0xFF00) >> 8) + (((x) & 0xFF) << 8))

BLEServer *pServer = NULL;
BLEService *pService = NULL;
BLECharacteristic *pTxCharacteristic;
BLECharacteristic *pRxCharacteristic;
bool bleDataTransferEnabled = false;

bool deviceConnected = false;
bool oldDeviceConnected = false;

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *pServer) { deviceConnected = true; };

    void onDisconnect(BLEServer *pServer) { deviceConnected = false; }
};

class MyCallbacks : public BLECharacteristicCallbacks {
    NimBLEAttValue data;
    void onWrite(NimBLECharacteristic *pCharacteristic) { data = pCharacteristic->getValue(); }
};

uint8_t sta_mac[6];
char strID[18];
char strAddl[200];

void ble_info(const String &name, const String &address, const String &signal) {
    // Legacy entry; BLE Scan uses scannerListShowDetail.
    (void)name;
    (void)address;
    (void)signal;
}

class AdvertisedDeviceCallbacks : public NimBLEScanCallbacks {};

static AdvertisedDeviceCallbacks g_scanCallbacks;

static bool is_ble_inited = false;

void stopBLEStack() {
    if (pBLEScan) {
        pBLEScan->stop();
        pBLEScan->clearResults();
        pBLEScan = nullptr;
    }

    if (is_ble_inited) {
#if !defined(LITE_VERSION)
        if (BLEStateManager::isBLEActive() || BLEStateManager::getActiveClientCount() > 0) {
            BLEStateManager::deinitBLE(true);
        } else
#endif
            if (BLEDevice::getScan() != nullptr || BLEDevice::getAdvertising() != nullptr ||
                BLEDevice::getServer() != nullptr || BLEConnected) {
            BLEDevice::deinit();
        }
    }

    pServer = nullptr;
    pService = nullptr;
    pTxCharacteristic = nullptr;
    pRxCharacteristic = nullptr;
    deviceConnected = false;
    oldDeviceConnected = false;
    bleDataTransferEnabled = false;
    is_ble_inited = false;
    BLEConnected = false;
#if !defined(LITE_VERSION)
    if (hid_ble) {
        delete hid_ble;
        hid_ble = nullptr;
    }
#endif
    uiRamLeaveHeavy();
}

bool ble_scan_setup() {
    if (FORCE_RADIO_TEARDOWN_ON_SWITCH) {
        if (WiFi.getMode() != WIFI_MODE_NULL || wifiConnected) {
            wifiDisconnect();
            delay(200);
        }

        stopBLEStack();
        delay(100);
    }

    RAM_LOG("ble-scan pre-init");

    // FIX: Always try to init - if already init'd, it's a no-op
    if (!radioHasMemForBle()) {
        displayError("Low RAM: free WiFi/SD first", true);
        returnToMenu = true;
        return false;
    }
    uiRamEnterHeavy();

    BLEDevice::init("");
    is_ble_inited = true;

    RAM_LOG("ble-scan post-init");
    pBLEScan = BLEDevice::getScan();
    if (!pBLEScan) {
        displayError("Failed to get scan object", true);
        return false;
    }

    pBLEScan->setScanCallbacks(&g_scanCallbacks);
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(SCAN_INT);
    pBLEScan->setWindow(SCAN_WINDOW);
    pBLEScan->setDuplicateFilter(false);

    esp_read_mac(sta_mac, ESP_MAC_BT);

    sprintf(
        strID,
        "%02X:%02X:%02X:%02X:%02X:%02X",
        sta_mac[0],
        sta_mac[1],
        sta_mac[2],
        sta_mac[3],
        sta_mac[4],
        sta_mac[5]
    );
    vTaskDelay(100 / portTICK_PERIOD_MS);
    return true;
}

namespace {

struct BleScanHit {
    String name;
    String mac;
    int rssi = 0;
    String addrType;
    String services;
    String mfgHex;
    String txPower;
    String appearance;
    String flags;
    String payloadHex;
    unsigned long firstSeen = 0;
    unsigned long lastSeen = 0;
};

String bleBytesToHex(const uint8_t *p, size_t len, size_t maxBytes = 12) {
    String out;
    size_t n = min(len, maxBytes);
    for (size_t i = 0; i < n; i++) {
        char b[4];
        snprintf(b, sizeof(b), "%02X", p[i]);
        out += b;
        if (i + 1 < n) out += " ";
    }
    if (len > maxBytes) out += "...";
    return out;
}

String bleAddrTypeName(const NimBLEAdvertisedDevice *dev) {
    if (!dev) return "?";
    switch (dev->getAddressType()) {
        case BLE_ADDR_PUBLIC: return "public";
        case BLE_ADDR_RANDOM: return "random";
        case BLE_ADDR_PUBLIC_ID: return "public-id";
        case BLE_ADDR_RANDOM_ID: return "random-id";
        default: return "other";
    }
}

void bleFillHit(BleScanHit &h, const NimBLEAdvertisedDevice *dev) {
    if (!dev) return;
    h.name = String(dev->getName().c_str());
    h.mac = String(dev->getAddress().toString().c_str());
    h.rssi = dev->getRSSI();
    h.addrType = bleAddrTypeName(dev);
    if (dev->haveServiceUUID()) h.services = String(dev->getServiceUUID().toString().c_str());
    if (dev->haveManufacturerData()) {
        std::string md = dev->getManufacturerData();
        h.mfgHex = bleBytesToHex((const uint8_t *)md.data(), md.size());
    }
    if (dev->haveTXPower()) h.txPower = String(dev->getTXPower()) + " dBm";
    if (dev->haveAppearance()) h.appearance = String(dev->getAppearance());
    {
        char fb[8];
        snprintf(fb, sizeof(fb), "0x%02X", (unsigned)dev->getAdvFlags());
        h.flags = fb;
    }
    const std::vector<uint8_t> &payload = dev->getPayload();
    if (!payload.empty()) h.payloadHex = bleBytesToHex(payload.data(), payload.size());
}

std::vector<String> bleScanRowLabels(const std::vector<BleScanHit> &hits) {
    std::vector<String> rows;
    rows.reserve(hits.size());
    for (const auto &h : hits) {
        String label = h.name.length() ? h.name : h.mac;
        label += "  " + String(h.rssi) + "dBm";
        rows.push_back(label);
    }
    return rows;
}

std::vector<ScannerDetailField> bleScanDetail(const BleScanHit &h) {
    std::vector<ScannerDetailField> f;
    f.push_back({"Name", h.name.length() ? h.name : "<no name>"});
    f.push_back({"MAC", h.mac});
    f.push_back({"RSSI", String(h.rssi) + " dBm"});
    f.push_back({"Addr type", h.addrType});
    if (h.services.length()) f.push_back({"Service", h.services});
    if (h.mfgHex.length()) f.push_back({"Mfg data", h.mfgHex});
    if (h.txPower.length()) f.push_back({"TX power", h.txPower});
    if (h.appearance.length()) f.push_back({"Appearance", h.appearance});
    if (h.flags.length()) f.push_back({"Flags", h.flags});
    if (h.payloadHex.length()) f.push_back({"Payload", h.payloadHex});
    f.push_back({"First seen", String(h.firstSeen) + " ms"});
    f.push_back({"Last seen", String(h.lastSeen) + " ms"});
    return f;
}

} // namespace

void ble_scan() {
    bool bleWasActiveBefore = BLEConnected || (BLEDevice::getServer() != nullptr);
#if !defined(LITE_VERSION)
    bleWasActiveBefore =
        bleWasActiveBefore || BLEStateManager::isBLEActive() || BLEStateManager::getActiveClientCount() > 0;
#endif

    if (!ble_scan_setup() || pBLEScan == nullptr) {
        displayError("Failed to init BLE scan");
        return;
    }

    pBLEScan->clearResults();
    pBLEScan->setDuplicateFilter(false);
    pBLEScan->setMaxResults(0xFF);
    pBLEScan->setScanResponseTimeout(200);
    pBLEScan->start(0, false);

    std::vector<BleScanHit> hits;
    std::set<String> seen;

    ScannerListState ui;
    scannerListBegin(ui, "BLE Scan", "scanning");

    auto ingest = [&]() {
        if (!pBLEScan) return;
        if (!pBLEScan->isScanning()) pBLEScan->start(0, true, true);
        NimBLEScanResults results = pBLEScan->getResults();
        int deviceCount = results.getCount();
        for (int i = 0; i < deviceCount; i++) {
            if ((int)hits.size() >= MAX_DISPLAY_DEVICES &&
                seen.find(String(results.getDevice(i)->getAddress().toString().c_str())) == seen.end()) {
                continue;
            }
            const NimBLEAdvertisedDevice *dev = results.getDevice(i);
            if (!dev) continue;
            String mac = String(dev->getAddress().toString().c_str());
            if (seen.insert(mac).second) {
                if ((int)hits.size() >= MAX_DISPLAY_DEVICES) {
                    seen.erase(mac);
                    continue;
                }
                BleScanHit h;
                bleFillHit(h, dev);
                h.firstSeen = millis();
                h.lastSeen = h.firstSeen;
                hits.push_back(h);
            } else {
                for (auto &h : hits) {
                    if (h.mac == mac) {
                        h.rssi = dev->getRSSI();
                        String name = String(dev->getName().c_str());
                        if (name.length()) h.name = name;
                        h.lastSeen = millis();
                        break;
                    }
                }
            }
        }
    };

    unsigned long lastPaint = 0;
    while (true) {
        ScannerListResult r = scannerListPoll(ui);
        if (r == SCANNER_LIST_EXIT) break;
        if (r == SCANNER_LIST_DETAIL && ui.cursor >= 0 && ui.cursor < (int)hits.size()) {
            BleScanHit snap = hits[ui.cursor];
            String title = snap.name.length() ? snap.name : "RESULT DETAILS";
            scannerListShowDetail(title.c_str(), bleScanDetail(snap), [&]() {
                if (pBLEScan && !pBLEScan->isScanning()) pBLEScan->start(0, true, true);
            });
            ui.cursor = min(ui.cursor, max(0, (int)hits.size() - 1));
            scannerListRefresh(ui);
            lastPaint = 0;
            continue;
        }
        if (millis() - lastPaint > 220) {
            lastPaint = millis();
            try {
                ingest();
            } catch (...) {
                displayError("BLE scan error");
                break;
            }
            scannerListSetStatus(ui, (String(hits.size()) + " devices").c_str());
            scannerListSetRows(ui, bleScanRowLabels(hits));
        }
        delay(20);
        if (forceHome) break;
    }

    scannerListEnd();
    if (pBLEScan) {
        pBLEScan->stop();
        pBLEScan->clearResults();
    }

    if (!bleWasActiveBefore) {
#if !defined(LITE_VERSION)
        if (!BLEStateManager::isBLEActive()) { stopBLEStack(); }
#else
        stopBLEStack();
#endif
    }

    if (hits.empty()) displayInfo("No devices found", true);
    else displayInfo("Found " + String((int)hits.size()) + " device(s)", true);
}

bool initBLEServer() {
    uint64_t chipid = ESP.getEfuseMac();
    String blename = "Bruce-" + String((uint8_t)(chipid >> 32), HEX);

    if (!is_ble_inited) {
        BLEDevice::init(blename.c_str());
        is_ble_inited = true;
    }

    pServer = BLEDevice::createServer();
    if (!pServer) {
        displayError("Failed to create BLE server");
        return false;
    }

    pServer->setCallbacks(new MyServerCallbacks());
    pService = pServer->createService(SERVICE_UUID);
    if (!pService) {
        displayError("Failed to create BLE service");
        return false;
    }

    pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_RX_UUID, NIMBLE_PROPERTY::NOTIFY);
    if (!pTxCharacteristic) {
        displayError("Failed to create TX characteristic");
        return false;
    }

    pTxCharacteristic->addDescriptor(new NimBLE2904());
    pRxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_TX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    if (!pRxCharacteristic) {
        displayError("Failed to create RX characteristic");
        return false;
    }
    pRxCharacteristic->setCallbacks(new MyCallbacks());

    return true;
}

void disPlayBLESend() {
    uint8_t senddata[2] = {0};
    tft.fillScreen(kvxConfig.bgColor);
    drawMainBorder();
    tft.setTextSize(1);

    if (!pServer) {
        if (!initBLEServer()) {
            displayError("Failed to init BLE server");
            return;
        }
    }

    pServer->getAdvertising()->start();

    uint64_t chipid = ESP.getEfuseMac();
    String blename = "Bruce-" + String((uint8_t)(chipid >> 32), HEX);

    BLEConnected = true;

    bool wasConnected = false;
    bool first_run = true;
    while (!check(EscPress)) {
        if (deviceConnected) {
            if (!wasConnected) {
                tft.fillRect(10, 26, tftWidth - 20, tftHeight - 36, TFT_BLACK);
                drawBLE_beacon(180, 28, TFT_BLUE);
                tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
                tft.setTextSize(FM);
                tft.setCursor(12, 50);
                tft.printf("BLE Send\n");
                tft.setTextSize(FM);
            }
            tft.fillRect(10, 100, tftWidth - 20, 28, TFT_BLACK);
            tft.setCursor(12, 100);
            if (senddata[0] % 4 == 0) {
                tft.printf("0x%02X>    ", senddata[0]);
            } else if (senddata[0] % 4 == 1) {
                tft.printf("0x%02X>>   ", senddata[0]);
            } else if (senddata[0] % 4 == 2) {
                tft.printf("0x%02X >>  ", senddata[0]);
            } else if (senddata[0] % 4 == 3) {
                tft.printf("0x%02X  >  ", senddata[0]);
            }

            senddata[1]++;
            if (senddata[1] > 3) {
                senddata[1] = 0;
                senddata[0]++;
                pTxCharacteristic->setValue(senddata, 1);
                pTxCharacteristic->notify();
            }
            wasConnected = true;
        } else {
            if (wasConnected or first_run) {
                first_run = false;
                tft.fillRect(10, 26, tftWidth - 20, tftHeight - 36, TFT_BLACK);
                tft.setTextSize(FM);
                tft.setCursor(12, 50);
                tft.setTextColor(TFT_RED);
                tft.printf("BLE disconnect\n");
                tft.setCursor(12, 75);
                tft.setTextColor(tft.color565(18, 150, 219));

                tft.printf(String("Name:" + blename + "\n").c_str());
                tft.setCursor(12, 100);
                tft.printf("UUID:1bc68b2a\n");
                drawBLE_beacon(180, 40, TFT_DARKGREY);
            }
            wasConnected = false;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    tft.setTextColor(TFT_WHITE);
    pServer->getAdvertising()->stop();
    BLEConnected = false;
}

void ble_test() {
    printf("ble test\n");

    if (!is_ble_inited) {
        printf("Init ble server\n");
        if (!initBLEServer()) {
            displayError("Failed to init BLE server");
            return;
        }
        delay(100);
    }

    disPlayBLESend();

    if (pServer) { pServer->getAdvertising()->stop(); }
    stopBLEStack();

    printf("Quit ble test\n");
}
