#include "root/hal/ble/ble_backend.h"

#if !defined(KVX_BLE_BACKEND_HOSTED)

namespace {

class BleBackendNimble : public BleBackend {
public:
    const char *name() const override { return "nimble-arduino"; }
    bool isSupported() const override { return true; }
    uint32_t features() const override {
        return BLE_FEAT_SCAN | BLE_FEAT_ADV | BLE_FEAT_GATT_CLIENT | BLE_FEAT_GATT_SERVER | BLE_FEAT_HID;
    }
    const char *unsupportedReason(BleFeature) const override { return "BLE ready"; }

    bool init() override { return true; }
    void deinit() override {}
    bool startScan() override { return false; }
    void stopScan() override {}
    int copyScan(BleScanResult *, int) override { return 0; }
    bool advertiseName(const char *) override { return false; }
    bool advertiseMfg(const char *, const uint8_t *, size_t) override { return false; }
    void stopAdvertise() override {}
};

BleBackendNimble gBackend;

} // namespace

BleBackend &bleBackend() { return gBackend; }

#endif
