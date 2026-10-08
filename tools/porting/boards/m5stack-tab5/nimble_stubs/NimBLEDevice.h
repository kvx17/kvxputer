#pragma once
// ESP32-P4 milestone-1 stub: NimBLE-Arduino does not build on P4.
#if !defined(KVX_NO_NIMBLE)
#error "nimble_stubs included without KVX_NO_NIMBLE"
#endif

#include <Arduino.h>
#include <esp_err.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// esp_gap_ble_api power levels / addr types are not present on P4; stubs for compile.
typedef int esp_power_level_t;

#ifndef NIMBLE_PROPERTY
enum {
    NIMBLE_PROPERTY_READ = 1,
    NIMBLE_PROPERTY_WRITE = 2,
    NIMBLE_PROPERTY_NOTIFY = 4,
    NIMBLE_PROPERTY_INDICATE = 8,
    NIMBLE_PROPERTY_WRITE_NR = 16,
};
#define NIMBLE_PROPERTY
#endif

#ifndef ESP_PWR_LVL_N12
enum {
    ESP_PWR_LVL_N12 = -12,
    ESP_PWR_LVL_N9 = -9,
    ESP_PWR_LVL_N6 = -6,
    ESP_PWR_LVL_N3 = -3,
    ESP_PWR_LVL_N0 = 0,
    ESP_PWR_LVL_P3 = 3,
    ESP_PWR_LVL_P6 = 6,
    ESP_PWR_LVL_P9 = 9,
};
#endif

#ifndef BLE_ADDR_PUBLIC
#define BLE_ADDR_PUBLIC 0x00
#define BLE_ADDR_RANDOM 0x01
#define BLE_ADDR_PUBLIC_ID 0x02
#define BLE_ADDR_RANDOM_ID 0x03
#endif

class NimBLEClient;

class NimBLEUUID {
public:
    NimBLEUUID() {}
    NimBLEUUID(const char *) {}
    NimBLEUUID(uint16_t) {}
    NimBLEUUID(uint32_t) {}
    std::string toString() const { return ""; }
};

class NimBLEAddress {
public:
    NimBLEAddress() { memset(_val, 0, sizeof(_val)); }
    NimBLEAddress(const std::string &, uint8_t = BLE_ADDR_PUBLIC) { memset(_val, 0, sizeof(_val)); }
    NimBLEAddress(const char *, uint8_t = BLE_ADDR_PUBLIC) { memset(_val, 0, sizeof(_val)); }
    std::string toString() const { return "00:00:00:00:00:00"; }
    const uint8_t *getVal() const { return _val; }
    uint8_t getType() const { return BLE_ADDR_PUBLIC; }
    bool operator==(const NimBLEAddress &) const { return false; }

private:
    uint8_t _val[6];
};

class NimBLEConnInfo {
public:
    NimBLEAddress getAddress() const { return {}; }
    uint16_t getConnHandle() const { return 0; }
    bool isEncrypted() const { return false; }
    bool isBonded() const { return false; }
};

class NimBLECharacteristic;
class NimBLEService;
class NimBLEServer;
class NimBLEAdvertising;
class NimBLEAdvertisementData;
class NimBLEHIDDevice;
class NimBLEScan;
class NimBLEAdvertisedDevice;
class NimBLEBeacon;
class NimBLEScanResults;

class NimBLEServerCallbacks {
public:
    virtual ~NimBLEServerCallbacks() {}
    virtual void onConnect(NimBLEServer *, NimBLEConnInfo &) {}
    virtual void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) {}
    virtual void onAuthenticationComplete(NimBLEConnInfo &) {}
};

class NimBLECharacteristicCallbacks {
public:
    virtual ~NimBLECharacteristicCallbacks() {}
    virtual void onWrite(NimBLECharacteristic *, NimBLEConnInfo &) {}
    virtual void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &, uint16_t) {}
    virtual void onRead(NimBLECharacteristic *, NimBLEConnInfo &) {}
};

class NimBLECharacteristic {
public:
    void setValue(const uint8_t *, size_t) {}
    void setValue(const std::string &) {}
    void setValue(uint8_t) {}
    std::string getValue() { return ""; }
    void addDescriptor(void *) {}
    bool notify(bool = true) { return false; }
    bool notify(const uint8_t *, size_t, bool = true) { return false; }
    bool indicate(bool = true) { return false; }
    bool indicate(const uint8_t *, size_t, bool = true) { return false; }
    void setCallbacks(NimBLECharacteristicCallbacks *) {}
    uint16_t getHandle() const { return 0; }
};

class NimBLEService {
public:
    NimBLEUUID getUUID() const { return {}; }
    NimBLECharacteristic *createCharacteristic(const char *, uint32_t) { return &_chr; }
    NimBLECharacteristic *createCharacteristic(uint16_t, uint32_t) { return &_chr; }
    void start() {}

private:
    NimBLECharacteristic _chr;
};

class NimBLEAdvertisementData {
public:
    void setName(const std::string &) {}
    void setFlags(uint8_t) {}
    void setManufacturerData(const std::string &) {}
    void setManufacturerData(const std::vector<uint8_t> &) {}
    void setManufacturerData(const uint8_t *, size_t) {}
    void setServiceData(const NimBLEUUID &, const std::string &) {}
    void addData(const std::string &) {}
    void addData(const uint8_t *, size_t) {}
    void addData(uint8_t *, int) {}
    void addData(const uint8_t *, int) {}
    void addData(uint8_t *, uint8_t) {}
    void setAppearance(uint16_t) {}
    std::string getPayload() const { return ""; }
};

class NimBLEAdvertising {
public:
    void start(bool = true) {}
    void stop() {}
    void setName(const std::string &) {}
    void addServiceUUID(const NimBLEUUID &) {}
    void addServiceUUID(const char *) {}
    void setAppearance(uint16_t) {}
    void setMinPreferred(uint16_t) {}
    void setScanResponse(bool) {}
    void setAdvertisementData(const NimBLEAdvertisementData &) {}
    void setScanResponseData(const NimBLEAdvertisementData &) {}
    void setMinInterval(uint16_t) {}
    void setMaxInterval(uint16_t) {}
    void setManufacturerData(const uint8_t *, size_t) {}
    void setManufacturerData(const std::string &) {}
    bool isAdvertising() const { return false; }
};

class NimBLEServer {
public:
    NimBLEService *createService(const char *) { return &_svc; }
    NimBLEService *createService(uint16_t) { return &_svc; }
    NimBLEService *createService(const NimBLEUUID &) { return &_svc; }
    void start() {}
    void setCallbacks(NimBLEServerCallbacks *) {}
    NimBLEAdvertising *getAdvertising() { return &_adv; }
    uint16_t getConnectedCount() const { return 0; }
    void disconnect(uint16_t) {}
    void updateConnParams(uint16_t, uint16_t, uint16_t, uint16_t, uint16_t) {}

private:
    NimBLEService _svc;
    NimBLEAdvertising _adv;
};

class NimBLEHIDDevice {
public:
    explicit NimBLEHIDDevice(NimBLEServer *) {}
    NimBLECharacteristic *getInputReport(uint8_t) { return &_chr; }
    NimBLECharacteristic *getOutputReport(uint8_t) { return &_chr; }
    NimBLECharacteristic *getFeatureReport(uint8_t) { return &_chr; }
    NimBLECharacteristic *getHidControl() { return &_chr; }
    NimBLECharacteristic *getPnp() { return &_chr; }
    NimBLECharacteristic *getManufacturer() { return &_chr; }
    NimBLEService *getHidService() { return &_svc; }
    NimBLEService *getDeviceInfoService() { return &_svc; }
    NimBLEService *getBatteryService() { return &_svc; }
    void startServices() {}
    void setManufacturer(const std::string &) {}
    void setPnp(uint8_t, uint16_t, uint16_t, uint16_t) {}
    void setHidInfo(uint8_t, uint8_t) {}
    void setReportMap(uint8_t *, size_t) {}
    void setBatteryLevel(uint8_t) {}

private:
    NimBLECharacteristic _chr;
    NimBLEService _svc;
};

class NimBLEAdvertisedDevice {
public:
    std::string getName() const { return ""; }
    NimBLEAddress getAddress() const { return {}; }
    uint8_t getAddressType() const { return BLE_ADDR_PUBLIC; }
    int getRSSI() const { return 0; }
    bool haveName() const { return false; }
    bool haveManufacturerData() const { return false; }
    std::string getManufacturerData() const { return ""; }
    bool haveServiceUUID() const { return false; }
    NimBLEUUID getServiceUUID() const { return {}; }
    bool haveTXPower() const { return false; }
    int getTXPower() const { return 0; }
    bool haveAppearance() const { return false; }
    uint16_t getAppearance() const { return 0; }
    uint8_t getAdvFlags() const { return 0; }
    bool isAdvertisingService(const NimBLEUUID &) const { return false; }
    std::string toString() const { return ""; }
    uint8_t getPayloadLength() const { return 0; }
    const std::vector<uint8_t> &getPayload() const { return _payload; }
    operator const NimBLEAdvertisedDevice *() const { return this; }

private:
    std::vector<uint8_t> _payload;
};

class NimBLEAdvertisedDeviceCallbacks {
public:
    virtual ~NimBLEAdvertisedDeviceCallbacks() {}
    virtual void onResult(NimBLEAdvertisedDevice *) {}
};

class NimBLEScanResults {
public:
    int getCount() const { return 0; }
    const NimBLEAdvertisedDevice *getDevice(int) const {
        static NimBLEAdvertisedDevice d;
        return &d;
    }
};

class NimBLEScanCallbacks {
public:
    virtual ~NimBLEScanCallbacks() {}
    virtual void onDiscovered(const NimBLEAdvertisedDevice *) {}
    virtual void onResult(const NimBLEAdvertisedDevice *) {}
    virtual void onScanEnd(NimBLEScanResults, int) {}
    virtual void onScanEnd(NimBLEScan *, int) {}
};

class NimBLEScan {
public:
    void setActiveScan(bool) {}
    void setInterval(uint16_t) {}
    void setWindow(uint16_t) {}
    void setDuplicateFilter(bool) {}
    void setMaxResults(uint8_t) {}
    void setScanResponseTimeout(uint16_t) {}
    void setAdvertisedDeviceCallbacks(NimBLEAdvertisedDeviceCallbacks *, bool = false) {}
    void setScanCallbacks(NimBLEScanCallbacks *, bool = false) {}
    bool start(uint32_t, bool = true, bool = false) { return false; }
    bool start(uint32_t, void (*)(NimBLEScanResults), bool = true) { return false; }
    void stop() {}
    void clearResults() {}
    bool isScanning() const { return false; }
    NimBLEScanResults getResults() { return {}; }
    NimBLEScanResults getResults(uint32_t, bool = false) { return {}; }
};

class NimBLERemoteCharacteristic {
public:
    bool writeValue(const uint8_t *, size_t, bool = false) { return false; }
    bool writeValue(const std::vector<uint8_t> &, bool = false) { return false; }
    bool subscribe(bool, std::function<void(NimBLERemoteCharacteristic *, uint8_t *, size_t, bool)>) {
        return false;
    }
    bool subscribe(bool, void (*)(NimBLERemoteCharacteristic *, uint8_t *, size_t, bool)) { return false; }
    uint16_t getHandle() const { return 0; }
    NimBLEUUID getUUID() const { return {}; }
    bool canWrite() const { return false; }
    bool canWriteNoResponse() const { return false; }
    bool canNotify() const { return false; }
    bool canIndicate() const { return false; }
    bool canRead() const { return false; }
    std::string readValue() { return ""; }
    bool writeValue(const std::string &, bool = false) { return false; }
};

class NimBLERemoteService {
public:
    NimBLEUUID getUUID() const { return {}; }
    NimBLERemoteCharacteristic *getCharacteristic(const NimBLEUUID &) { return &_chr; }
    NimBLERemoteCharacteristic *getCharacteristic(const char *) { return &_chr; }
    std::vector<NimBLERemoteCharacteristic *> getCharacteristics(bool = false) { return {}; }

private:
    NimBLERemoteCharacteristic _chr;
};

class NimBLEClientCallbacks {
public:
    virtual ~NimBLEClientCallbacks() {}
    virtual void onConnect(NimBLEClient *) {}
    virtual void onDisconnect(NimBLEClient *, int) {}
};

class NimBLEClient {
public:
    bool connect(const NimBLEAdvertisedDevice *) { return false; }
    bool connect(const NimBLEAddress &) { return false; }
    bool connect(const NimBLEAddress &, bool) { return false; }
    void disconnect() {}
    bool isConnected() const { return false; }
    void setClientCallbacks(NimBLEClientCallbacks *, bool = false) {}
    void setConnectTimeout(uint32_t) {}
    void setConnectionParams(uint16_t, uint16_t, uint16_t, uint16_t) {}
    bool discoverAttributes() { return false; }
    bool secureConnection() { return false; }
    std::vector<NimBLERemoteService *> getServices(bool = false) { return {}; }
    NimBLERemoteService *getService(const NimBLEUUID &) { return &_svc; }
    NimBLERemoteService *getService(const char *) { return &_svc; }
    void setClientCallbacks(NimBLEClientCallbacks *) {}
    NimBLEAddress getPeerAddress() const { return {}; }

private:
    NimBLERemoteService _svc;
};

class NimBLEBeacon {
public:
    void setManufacturerId(uint16_t) {}
    void setMajor(uint16_t) {}
    void setMinor(uint16_t) {}
    void setSignalPower(int8_t) {}
    void setProximityUUID(const NimBLEUUID &) {}
    const std::vector<uint8_t> &getData() const { return _data; }

private:
    std::vector<uint8_t> _data;
};

class NimBLEDevice {
public:
    // Return false so callers that check init() skip BLE (P4 has no NimBLE).
    static bool init(const std::string & = "") { return false; }
    static void deinit(bool = true) {}
    static bool isInitialized() { return false; }
    static NimBLEServer *getServer() { return nullptr; }
    static NimBLEServer *createServer() {
        static NimBLEServer s;
        return &s;
    }
    static NimBLEAdvertising *getAdvertising() {
        static NimBLEAdvertising a;
        return &a;
    }
    static NimBLEScan *getScan() {
        static NimBLEScan s;
        return &s;
    }
    static NimBLEClient *createClient() {
        static NimBLEClient c;
        return &c;
    }
    static void deleteClient(NimBLEClient *) {}
    static void setSecurityAuth(bool, bool, bool) {}
    static void setSecurityIOCap(uint8_t) {}
    static void setPower(int) {}
    static void setDeviceName(const std::string &) {}
    static std::string getAddress() { return ""; }
    static void whiteListAdd(const NimBLEAddress &) {}
    static void whiteListRemove(const NimBLEAddress &) {}
    static bool onWhiteList(const NimBLEAddress &) { return false; }
    static void setOwnAddrType(uint8_t) {}
    static void deleteAllBonds() {}
    static int getNumBonds() { return 0; }
    static NimBLEAddress getBondedAddress(int) { return {}; }
    static void deleteBond(const NimBLEAddress &) {}
};

namespace NimBLEUtils {
inline void dumpGapEvent(void *, void *) {}
}

using BLEDevice = NimBLEDevice;
using BLEServer = NimBLEServer;
using BLEAdvertising = NimBLEAdvertising;
using BLEScan = NimBLEScan;
using BLEScanResults = NimBLEScanResults;
using BLEAdvertisedDevice = NimBLEAdvertisedDevice;
using BLECharacteristic = NimBLECharacteristic;
using BLEHIDDevice = NimBLEHIDDevice;
using BLEServerCallbacks = NimBLEServerCallbacks;
using BLECharacteristicCallbacks = NimBLECharacteristicCallbacks;
using BLEUUID = NimBLEUUID;
using BLEService = NimBLEService;
using BLEClient = NimBLEClient;
using BLEAddress = NimBLEAddress;
using BLEBeacon = NimBLEBeacon;
using BLERemoteService = NimBLERemoteService;
using BLERemoteCharacteristic = NimBLERemoteCharacteristic;
using BLEAdvertisementData = NimBLEAdvertisementData;
using BLEScanCallbacks = NimBLEScanCallbacks;
using BLEAdvertisedDeviceCallbacks = NimBLEAdvertisedDeviceCallbacks;

// Bluedroid helpers used by BLE spam; not present in the P4 hosted headers.
#ifndef ESP_BLE_PWR_TYPE_ADV
#define ESP_BLE_PWR_TYPE_ADV 5
#endif
inline esp_err_t esp_ble_tx_power_set(int, int) { return ESP_ERR_NOT_SUPPORTED; }
inline esp_err_t esp_ble_gap_set_rand_addr(const uint8_t *) { return ESP_ERR_NOT_SUPPORTED; }
