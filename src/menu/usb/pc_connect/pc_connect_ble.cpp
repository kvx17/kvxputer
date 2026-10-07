#include "pc_connect.h"

#if !defined(LITE_VERSION)

#include "root/ui/scanner_list.h"
#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <map>

#if defined(EVIL_EXTENSIONS)
#include "menu/ble/skimmer/skimmer.h"
#include "menu/ble/wall_of_airtag/wall_of_airtag.h"
#include "menu/wifi/wall_of_flipper/wall_of_flipper.h"
#endif

namespace {

enum class BleMode : uint8_t { None = 0, Scan, Flipper, Airtag, Skimmer };

bool g_active = false;
BleMode g_mode = BleMode::None;
NimBLEScan *g_scan = nullptr;

struct DevTrack {
    int rssi = -999;
    String name;
    uint32_t lastEmit = 0;
};
std::map<String, DevTrack> g_seen;

String addrTypeName(uint8_t t) {
    switch (t) {
        case BLE_ADDR_PUBLIC: return "public";
        case BLE_ADDR_RANDOM: return "random";
        case BLE_ADDR_PUBLIC_ID: return "public-id";
        case BLE_ADDR_RANDOM_ID: return "random-id";
        default: return "other";
    }
}

String mfgHex(const ScannerAdvSnap &dev) {
    if (!dev.haveMfg || dev.mfg.empty()) return String();
    String out;
    size_t n = min(dev.mfg.size(), (size_t)12);
    for (size_t i = 0; i < n; i++) {
        char b[4];
        snprintf(b, sizeof(b), "%02X", (uint8_t)dev.mfg[i]);
        out += b;
        if (i + 1 < n) out += " ";
    }
    return out;
}

bool shouldEmit(const String &mac, int rssi, const String &name, bool force) {
    auto it = g_seen.find(mac);
    uint32_t now = millis();
    if (it == g_seen.end()) {
        if ((int)g_seen.size() >= 100) return false;
        g_seen[mac] = {rssi, name, now};
        return true;
    }
    bool nameChanged = name.length() && name != it->second.name;
    bool rssiChanged = rssi != it->second.rssi;
    if (force || nameChanged) {
        it->second.rssi = rssi;
        if (name.length()) it->second.name = name;
        it->second.lastEmit = now;
        return true;
    }
    if (rssiChanged && now - it->second.lastEmit >= 1000) {
        it->second.rssi = rssi;
        it->second.lastEmit = now;
        return true;
    }
    return false;
}

void teardownScan() {
    if (g_scan) {
        scannerBleTeardown(true);
        g_scan = nullptr;
    }
    g_seen.clear();
    g_active = false;
    g_mode = BleMode::None;
}

bool startScan(BleMode mode, const char *modeName) {
    pcConnectStopRadio();
    g_seen.clear();
    g_scan = scannerBleStart();
    if (!g_scan) return false;
    g_active = true;
    g_mode = mode;
    pcConnectSetStatus(PcRadio::Ble, modeName, 0);
    return true;
}

void emitBle(const ScannerAdvSnap &dev) {
    if (!shouldEmit(dev.mac, dev.rssi, dev.name, false)) return;
    JsonDocument doc;
    doc["evt"] = "ble";
    doc["mac"] = dev.mac;
    doc["name"] = dev.name;
    doc["rssi"] = dev.rssi;
    doc["addr_type"] = addrTypeName(dev.addrType);
    doc["services"] = dev.serviceUUID;
    String mfg = mfgHex(dev);
    if (mfg.length()) doc["mfg_hex"] = mfg;
    else doc["mfg_hex"] = nullptr;
    if (dev.haveTXPower) doc["tx_power"] = dev.txPower;
    else doc["tx_power"] = nullptr;
    if (dev.haveAppearance) doc["appearance"] = dev.appearance;
    else doc["appearance"] = nullptr;
    String out;
    serializeJson(doc, out);
    pcConnectEmitJson(out);
}

#if defined(EVIL_EXTENSIONS)
void emitFlipper(const ScannerAdvSnap &dev) {
    if (!looksLikeFlipper(dev)) return;
    if (!shouldEmit(dev.mac, dev.rssi, dev.name, true)) return;
    JsonDocument doc;
    doc["evt"] = "flipper";
    doc["mac"] = dev.mac;
    doc["name"] = dev.name;
    doc["rssi"] = dev.rssi;
    doc["addr_type"] = addrTypeName(dev.addrType);
    doc["services"] = dev.serviceUUID;
    if (dev.haveTXPower) doc["tx_power"] = String(dev.txPower) + " dBm";
    else doc["tx_power"] = nullptr;
    if (dev.haveAppearance) doc["appearance"] = String(dev.appearance);
    else doc["appearance"] = nullptr;
    String mfg = mfgHex(dev);
    if (mfg.length()) doc["mfg_hex"] = mfg;
    else doc["mfg_hex"] = nullptr;
    String out;
    serializeJson(doc, out);
    pcConnectEmitJson(out);
}

void emitAirtag(const ScannerAdvSnap &dev) {
    FindMyParse parsed;
    if (!parseFindMy(dev, parsed)) return;
    if (!shouldEmit(dev.mac, dev.rssi, "", true)) return;
    JsonDocument doc;
    doc["evt"] = "airtag";
    doc["mac"] = dev.mac;
    doc["rssi"] = dev.rssi;
    doc["battery"] = findMyBatteryLabel(parsed.battery);
    doc["separated"] = parsed.separated;
    doc["random"] = (dev.addrType != BLE_ADDR_PUBLIC);
    doc["key_prefix"] = parsed.keyPrefix;
    String out;
    serializeJson(doc, out);
    pcConnectEmitJson(out);
}

void emitSkimmer(const ScannerAdvSnap &dev) {
    const char *rule = skimmerMatchRule(dev.name, dev.mac);
    // Emit all devices; matched flag distinguishes hits
    if (!shouldEmit(dev.mac, dev.rssi, dev.name, rule != nullptr)) return;
    JsonDocument doc;
    doc["evt"] = "skimmer";
    doc["mac"] = dev.mac;
    doc["name"] = dev.name;
    doc["rssi"] = dev.rssi;
    doc["matched"] = rule != nullptr;
    doc["rule"] = rule ? rule : "";
    doc["addr_type"] = addrTypeName(dev.addrType);
    String out;
    serializeJson(doc, out);
    pcConnectEmitJson(out);
}
#endif

void processInbox() {
    if (!g_scan || !g_active) return;
    scannerBleKeepAlive(g_scan);
    const auto batch = scannerBleTakeInbox(g_scan);
    for (const auto &dev : batch) {
        switch (g_mode) {
            case BleMode::Scan: emitBle(dev); break;
#if defined(EVIL_EXTENSIONS)
            case BleMode::Flipper: emitFlipper(dev); break;
            case BleMode::Airtag: emitAirtag(dev); break;
            case BleMode::Skimmer: emitSkimmer(dev); break;
#endif
            default: break;
        }
    }
}

} // namespace

bool pcConnectBleScanActive() { return g_active && g_mode == BleMode::Scan; }

bool pcConnectBleScanStart() { return startScan(BleMode::Scan, "ble.scan"); }

void pcConnectBleScanStop() {
    if (!g_active) return;
    teardownScan();
}

void pcConnectBleScanTick() {
    if (g_mode != BleMode::Scan) return;
    processInbox();
}

#if defined(EVIL_EXTENSIONS)

bool pcConnectBleFlipperStart() { return startScan(BleMode::Flipper, "ble.flipper"); }
void pcConnectBleFlipperTick() {
    if (g_mode != BleMode::Flipper) return;
    processInbox();
}

bool pcConnectBleAirtagStart() { return startScan(BleMode::Airtag, "ble.airtag"); }
void pcConnectBleAirtagTick() {
    if (g_mode != BleMode::Airtag) return;
    processInbox();
}

bool pcConnectBleSkimmerStart() { return startScan(BleMode::Skimmer, "ble.skimmer"); }
void pcConnectBleSkimmerTick() {
    if (g_mode != BleMode::Skimmer) return;
    processInbox();
}

#endif // EVIL_EXTENSIONS

#endif // !LITE_VERSION
