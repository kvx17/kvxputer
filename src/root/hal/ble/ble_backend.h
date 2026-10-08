#pragma once
// Compile-selected BLE radio. Cardputer / StickS3 keep NimBLE-Arduino.
// Tab5 (KVX_BLE_BACKEND_HOSTED) uses the ESP-Hosted C6 controller + IDF NimBLE host.

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

enum BleFeature : uint32_t {
    BLE_FEAT_NONE = 0,
    BLE_FEAT_SCAN = 1u << 0,
    BLE_FEAT_ADV = 1u << 1,
    BLE_FEAT_GATT_CLIENT = 1u << 2,
    BLE_FEAT_GATT_SERVER = 1u << 3,
    BLE_FEAT_HID = 1u << 4,
};

struct BleScanResult {
    char addr[18];
    char name[32];
    int8_t rssi;
    uint8_t addrType;
    uint8_t mfg[31];
    uint8_t mfgLen;
};

class BleBackend {
public:
    virtual ~BleBackend() = default;
    virtual const char *name() const = 0;
    virtual bool isSupported() const = 0;
    virtual uint32_t features() const = 0;
    bool has(BleFeature f) const { return (features() & (uint32_t)f) != 0; }
    virtual const char *unsupportedReason(BleFeature f) const = 0;

    virtual bool init() = 0;
    virtual void deinit() = 0;

    virtual bool startScan() = 0;
    virtual void stopScan() = 0;
    virtual int copyScan(BleScanResult *out, int max) = 0;

    virtual bool advertiseName(const char *name) = 0;
    virtual bool advertiseMfg(const char *name, const uint8_t *mfg, size_t mfgLen) = 0;
    virtual void stopAdvertise() = 0;
};

BleBackend &bleBackend();

// True when the feature is implemented. Otherwise shows one displayError and returns false.
// On NimBLE-Arduino boards every feature is set, so this is a no-op.
bool bleFeatureOrExplain(BleFeature feature, const char *what);

// Apps still written against NimBLE-Arduino profiles (spam packets, GATT HID, suite).
// No-op when KVX_NO_NIMBLE is unset.
bool bleNimbleProfileOrExplain(const char *what);

#if defined(KVX_BLE_BACKEND_HOSTED)
void bleHostedScanApp();
#endif
