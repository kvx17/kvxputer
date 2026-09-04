#include "hid_remote_transport.h"
#include "menu/others/badusb_ble/ducky_typer.h"
#include "root/hal/radio_mem.h"
#include "root/ui/display.h"
#include "root/config/config.h"
#include <KeyboardLayout.h>
#include <NimBLEDevice.h>
#include <NimBLEAdvertisementData.h>
#include <NimBLEServer.h>
#include <esp_mac.h>
#if defined(USB_as_HID)
#include <USB.h>
#include "tusb.h"
#endif
#include <esp_random.h>
#include <globals.h>

HidRemoteTransportSession gHidRemoteSession;

static bool gRandomHidMac = false;

static void applyGenericUsbIdentity() {
#if defined(USB_as_HID)
    USB.manufacturerName("Generic");
    USB.productName("HID Keyboard");
    USB.serialNumber("1");
    // pid.codes VID: generic HID, not Espressif/M5/Apple
    USB.VID(0x1209);
    USB.PID(0x0001);
#endif
}

static void setHidRemoteBleMac() {
    // Locally administered unicast MAC (not a vendor OUI)
    uint8_t mac[6];
    uint64_t e = ESP.getEfuseMac();
    mac[0] = 0x02;
    mac[1] = (uint8_t)(e >> 32);
    mac[2] = (uint8_t)(e >> 24);
    mac[3] = (uint8_t)(e >> 16);
    mac[4] = (uint8_t)(e >> 8);
    mac[5] = (uint8_t)e;
    if (gRandomHidMac) {
        uint32_t r = (uint32_t)esp_random();
        mac[3] = (uint8_t)(r >> 16);
        mac[4] = (uint8_t)(r >> 8);
        mac[5] = (uint8_t)r;
        gRandomHidMac = false;
    }
#ifdef ESP_MAC_BT
    esp_iface_mac_addr_set(mac, ESP_MAC_BT);
#else
    (void)mac;
#endif
}

static bool ensureUsbKeyboard(HidRemoteTransportSession &s) {
#if defined(USB_as_HID)
    if (s.usbKeyboard == nullptr) s.usbKeyboard = new USBHIDKeyboard();
    if (!s.keyboardActive) {
        if (!s.mouseActive) {
            applyGenericUsbIdentity();
            USB.begin();
        }
        while (!tud_mounted() && !check(EscPress)) delay(50);
        if (check(EscPress)) return false;
        s.usbKeyboard->begin();
        s.keyboardHid = s.usbKeyboard;
        s.keyboardActive = true;
    }
    s.connected = true;
    return true;
#else
    (void)s;
    return false;
#endif
}

static bool ensureUsbMouse(HidRemoteTransportSession &s) {
#if defined(USB_as_HID)
    if (s.usbMouse == nullptr) s.usbMouse = new USBHIDMouse();
    if (!s.mouseActive) {
        if (!s.keyboardActive) {
            applyGenericUsbIdentity();
            USB.begin();
        }
        while (!tud_mounted() && !check(EscPress)) delay(50);
        if (check(EscPress)) return false;
        s.usbMouse->begin();
        s.mouseActive = true;
    }
    s.connected = true;
    return true;
#else
    (void)s;
    return false;
#endif
}

static void teardownUsb(HidRemoteTransportSession &s) {
#if defined(USB_as_HID)
    if (s.usbKeyboard != nullptr && s.keyboardActive) {
        s.usbKeyboard->end();
        s.keyboardActive = false;
    }
    if (s.usbMouse != nullptr && s.mouseActive) {
        s.usbMouse->end();
        s.mouseActive = false;
    }
    s.keyboardHid = nullptr;
    USB.~ESPUSB();
    delay(50);
    USB.enableDFU();
#endif
    s.connected = false;
}

static bool ensureBle(HidRemoteTransportSession &s) {
#if defined(CONFIG_BT_ENABLED)
    if (s.bleHid != nullptr && s.isConnected()) {
        s.keyboardHid = s.bleHid;
        s.keyboardActive = true;
        s.mouseActive = true;
        s.connected = true;
        return true;
    }

    if (!radioHasMemForBle()) {
        displayError("Low RAM: free WiFi/SD first", true);
        return false;
    }

    // Tear down any leftover BadUSB BLE pointer without double-deleting ours
#if !defined(LITE_VERSION)
    if (hid_ble != nullptr && hid_ble != s.bleHid) {
        safeCleanupDuckyBLE(hid_ble);
    } else {
        hid_ble = nullptr;
    }
#endif

    if (s.bleHid != nullptr) {
        s.bleHid->end();
        delete s.bleHid;
        s.bleHid = nullptr;
    }

    setHidRemoteBleMac();

    String deviceName = kvxConfig.hidRemoteBleName;
    if (deviceName.isEmpty()) deviceName = KVXKEYBOARD_HID_NAME;

    if (NimBLEDevice::isInitialized()) {
        NimBLEDevice::deinit(true);
        delay(100);
    }

    s.bleHid = new BleCompositeHid(deviceName, "HID", 100);
    s.bleHid->setName(deviceName);
    s.bleHid->set_vendor_id(0x0000);
    s.bleHid->set_product_id(0x0001);
    s.bleHid->set_version(0x0100);
    s.bleHid->setAppearence(0x03C0);
    const uint8_t *layout = KeyboardLayout_en_US;
    s.bleHid->begin(layout);
    s.bleHid->setDelay(kvxConfig.badUSBBLEKeyDelay);
    s.keyboardHid = s.bleHid;
    s.keyboardActive = true;
    s.mouseActive = true;
#if !defined(LITE_VERSION)
    hid_ble = s.bleHid;
#endif
    s.connected = false;
    BLEConnected = false;

    // Open discoverable advertising with full HID ADV payload
    s.advertiseOpen();
    return true;
#else
    (void)s;
    return false;
#endif
}

static void teardownBle(HidRemoteTransportSession &s) {
#if defined(CONFIG_BT_ENABLED)
    BleCompositeHid *p = s.bleHid;
    s.bleHid = nullptr;
    s.keyboardHid = nullptr;
    s.keyboardActive = false;
    s.mouseActive = false;
    s.connected = false;
    BLEConnected = false;
#if !defined(LITE_VERSION)
    if (hid_ble == p) hid_ble = nullptr;
#endif
    if (p != nullptr) {
        p->end();
        delete p;
    } else if (NimBLEDevice::isInitialized()) {
        NimBLEDevice::deinit(true);
    }
    delay(50);
#endif
}

bool HidRemoteTransportSession::begin(HidRemoteTransport t, HidRemoteCapability caps) {
    end();
    transport = t;
    connected = false;

    if (t == HID_REMOTE_USB) {
        bool ok = true;
        if (caps & HID_CAP_KEYBOARD) ok = ok && ensureUsbKeyboard(*this);
        if (caps & HID_CAP_MOUSE) ok = ok && ensureUsbMouse(*this);
        connected = ok;
        if (ok) refreshHostLabel();
        return ok;
    }

    if (t == HID_REMOTE_BLE) {
        bool ok = ensureBle(*this);
        if (ok) refreshHostLabel();
        return ok;
    }

    return false;
}

void HidRemoteTransportSession::end() {
    if (transport == HID_REMOTE_USB) teardownUsb(*this);
    else teardownBle(*this);
    connected = false;
    hostLabel = "";
}

#if defined(CONFIG_BT_ENABLED)
static bool bleAddrEqual(const String &a, const String &b) {
    if (a.isEmpty() || b.isEmpty()) return false;
    return a.equalsIgnoreCase(b);
}

static int bondIndexForStoredAddr(const String &addr) {
    if (addr.isEmpty() || !NimBLEDevice::isInitialized()) return -1;
    const int n = NimBLEDevice::getNumBonds();
    for (int i = 0; i < n; i++) {
        if (bleAddrEqual(addr, String(NimBLEDevice::getBondedAddress(i).toString().c_str()))) {
            return i;
        }
    }
    return -1;
}

static int bondIndexForConnection() {
    if (!NimBLEDevice::isInitialized()) return -1;
    NimBLEServer *server = NimBLEDevice::getServer();
    if (server == nullptr || server->getConnectedCount() == 0) return -1;
    NimBLEConnInfo info = server->getPeerInfo(0);
    String peer = String(info.getAddress().toString().c_str());
    String id = String(info.getIdAddress().toString().c_str());
    const int n = NimBLEDevice::getNumBonds();
    for (int i = 0; i < n; i++) {
        NimBLEAddress b = NimBLEDevice::getBondedAddress(i);
        String bonded = String(b.toString().c_str());
        if (bleAddrEqual(bonded, peer) || bleAddrEqual(bonded, id)) return i;
        if (b == info.getAddress() || b == info.getIdAddress()) return i;
    }
    return -1;
}

// True if the live GAP peer matches a stored/bond address (ID, RPA, or same bond index).
static bool connectionMatchesAddr(const String &expected) {
    if (expected.isEmpty() || !NimBLEDevice::isInitialized()) return false;
    NimBLEServer *server = NimBLEDevice::getServer();
    if (server == nullptr || server->getConnectedCount() == 0) return false;
    NimBLEConnInfo info = server->getPeerInfo(0);
    String peer = String(info.getAddress().toString().c_str());
    String id = String(info.getIdAddress().toString().c_str());
    if (bleAddrEqual(expected, peer) || bleAddrEqual(expected, id)) return true;

    const int want = bondIndexForStoredAddr(expected);
    const int got = bondIndexForConnection();
    if (want >= 0 && got >= 0 && want == got) return true;

    // Expected string may be a prior ID while bond table lists another form — still
    // accept when the live peer is that bond entry.
    if (want >= 0) {
        NimBLEAddress b = NimBLEDevice::getBondedAddress(want);
        if (b == info.getAddress() || b == info.getIdAddress()) return true;
    }
    return false;
}

static int findSlotMatchingConnection() {
    for (int slot = 1; slot <= KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT; slot++) {
        String slotted = kvxConfig.getHidRemoteHostSlot(slot);
        if (slotted.length() && connectionMatchesAddr(slotted)) return slot;
    }
    return 0;
}

static bool buildHidAdvertisement(NimBLEAdvertising *adv) {
    if (adv == nullptr) return false;
    String deviceName = kvxConfig.hidRemoteBleName;
    if (deviceName.isEmpty()) deviceName = KVXKEYBOARD_HID_NAME;
    NimBLEDevice::setDeviceName(std::string(deviceName.c_str()));

    NimBLEAdvertisementData advData;
    advData.setFlags(BLE_HS_ADV_F_DISC_GEN);
    advData.setAppearance(HID_REMOTE_BLE_APPEARANCE);
    advData.addServiceUUID(NimBLEUUID((uint16_t)0x1812));
    if (deviceName.length() <= 8) {
        advData.setName(std::string(deviceName.c_str()), true);
    } else {
        advData.setName(std::string(deviceName.substring(0, 8).c_str()), false);
    }
    adv->setAdvertisementData(advData);

    NimBLEAdvertisementData scanData;
    scanData.setName(std::string(deviceName.c_str()), true);
    adv->setScanResponseData(scanData);
    adv->setAppearance(HID_REMOTE_BLE_APPEARANCE);
    adv->setDiscoverableMode(BLE_GAP_DISC_MODE_GEN);
    adv->setConnectableMode(BLE_GAP_CONN_MODE_UND);
    adv->enableScanResponse(true);
    return true;
}
#endif

bool HidRemoteTransportSession::waitConnected(unsigned long timeoutMs) {
    // Accept any live link (startup soft-reconnect / USB)
    return waitConnectedExpected(String("__any__"), timeoutMs);
}

bool HidRemoteTransportSession::waitConnectedExpected(
    const String &expectedAddr, unsigned long timeoutMs, const String &excludeAddr
) {
    if (transport == HID_REMOTE_USB) {
        connected = keyboardActive || mouseActive;
        return connected;
    }
#if defined(CONFIG_BT_ENABLED)
    const bool acceptAny = expectedAddr == "__any__";
    const bool acceptNewOnly = expectedAddr.isEmpty();
    unsigned long start = millis();
    unsigned long lastReject = 0;
    unsigned long lastAdvKick = 0;

    String priorBonds[KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT];
    int priorBondCount = 0;
    if (acceptNewOnly && NimBLEDevice::isInitialized()) {
        const int n = NimBLEDevice::getNumBonds();
        for (int i = 0; i < n && priorBondCount < KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT; i++) {
            priorBonds[priorBondCount++] =
                String(NimBLEDevice::getBondedAddress(i).toString().c_str());
        }
    }

    auto wasPriorBond = [&](const String &addr) -> bool {
        for (int i = 0; i < priorBondCount; i++) {
            if (priorBonds[i].equalsIgnoreCase(addr)) return true;
        }
        return false;
    };

    auto isExcluded = [&]() -> bool {
        if (excludeAddr.isEmpty()) return false;
        return connectionMatchesAddr(excludeAddr);
    };

    while (!check(EscPress)) {
        NimBLEServer *server = NimBLEDevice::isInitialized() ? NimBLEDevice::getServer() : nullptr;
        const int gapLinks = (server != nullptr) ? (int)server->getConnectedCount() : 0;

        if (gapLinks > 0) {
            if (isConnected()) {
                // Always refuse the host we just left when switching slots
                if (isExcluded()) {
                    if ((millis() - lastReject) > 400) {
                        disconnectHost(false);
                        delay(200);
                        if (!acceptAny && expectedAddr.length() &&
                            bondIndexForStoredAddr(expectedAddr) >= 0) {
                            advertiseForHost(expectedAddr, true);
                        } else {
                            advertiseOpen();
                        }
                        lastReject = millis();
                        lastAdvKick = millis();
                    }
                    delay(40);
                    continue;
                }

                String addr = getConnectedAddress();
                bool ok = false;
                if (acceptAny) {
                    ok = true;
                } else if (acceptNewOnly) {
                    if (addr.isEmpty()) {
                        delay(40);
                        continue;
                    }
                    ok = kvxConfig.findHidRemoteHostSlotForAddr(addr) == 0 && !wasPriorBond(addr);
                    if (ok) {
                        for (int i = 0; i < priorBondCount; i++) {
                            if (connectionMatchesAddr(priorBonds[i])) {
                                ok = false;
                                break;
                            }
                        }
                    }
                    for (int slot = 1; ok && slot <= KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT; slot++) {
                        String slotted = kvxConfig.getHidRemoteHostSlot(slot);
                        if (slotted.length() && connectionMatchesAddr(slotted)) ok = false;
                    }
                } else {
                    // Strict: only the requested host (match addr / bond index / target slot)
                    if (addr.isEmpty()) {
                        delay(40);
                        continue;
                    }
                    ok = connectionMatchesAddr(expectedAddr);
                    if (!ok) {
                        const int liveSlot = findSlotMatchingConnection();
                        const int wantSlot = kvxConfig.findHidRemoteHostSlotForAddr(expectedAddr);
                        const int be = bondIndexForStoredAddr(expectedAddr);
                        const int bc = bondIndexForConnection();
                        if (wantSlot > 0 && liveSlot == wantSlot) ok = true;
                        else if (be >= 0 && bc == be) ok = true;
                    }
                }

                if (ok) {
                    BLEConnected = true;
                    connected = true;
                    rememberConnectedHost();
                    refreshHostLabel();
                    return true;
                }

                bool mustReject = false;
                if (acceptNewOnly) {
                    mustReject = true;
                } else if (!acceptAny && expectedAddr.length()) {
                    const int liveSlot = findSlotMatchingConnection();
                    const int wantSlot = kvxConfig.findHidRemoteHostSlotForAddr(expectedAddr);
                    // Wrong slot, or bonded peer that is not the target
                    if (liveSlot > 0 && wantSlot > 0 && liveSlot != wantSlot) mustReject = true;
                    else if (!connectionMatchesAddr(expectedAddr)) {
                        const int be = bondIndexForStoredAddr(expectedAddr);
                        const int bc = bondIndexForConnection();
                        if (bc >= 0 && (be < 0 || be != bc)) mustReject = true;
                    }
                }

                if (mustReject && (millis() - lastReject) > 500) {
                    disconnectHost(false);
                    delay(200);
                    if (!acceptAny && expectedAddr.length() &&
                        bondIndexForStoredAddr(expectedAddr) >= 0) {
                        advertiseForHost(expectedAddr, true);
                    } else {
                        advertiseOpen();
                    }
                    lastReject = millis();
                    lastAdvKick = millis();
                }
            }
            delay(40);
            continue;
        }

        if ((millis() - lastAdvKick) > 2500) {
            NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
            if (adv != nullptr && !adv->isAdvertising()) {
                if (!acceptAny && expectedAddr.length() &&
                    bondIndexForStoredAddr(expectedAddr) >= 0) {
                    advertiseForHost(expectedAddr, true);
                } else if (acceptNewOnly || (!acceptAny && expectedAddr.length())) {
                    advertiseOpen();
                } else {
                    ensureAdvertising();
                }
            }
            lastAdvKick = millis();
        }

        if (timeoutMs > 0 && (millis() - start) >= timeoutMs) break;
        delay(40);
    }
    connected = false;
    BLEConnected = false;
#else
    (void)excludeAddr;
#endif
    return false;
}

bool HidRemoteTransportSession::isConnected() {
    if (transport == HID_REMOTE_USB) return connected;
#if defined(CONFIG_BT_ENABLED)
    if (bleHid == nullptr || !NimBLEDevice::isInitialized()) {
        connected = false;
        BLEConnected = false;
        hostLabel = "";
        return false;
    }

    NimBLEServer *server = NimBLEDevice::getServer();
    const int gapLinks = (server != nullptr) ? (int)server->getConnectedCount() : 0;
    if (gapLinks <= 0) {
        if (bleHid->isConnected()) bleHid->clearConnected();
        connected = false;
        BLEConnected = false;
        hostLabel = "";
        return false;
    }

    NimBLEConnInfo info = server->getPeerInfo(0);
    // Require a usable HID link: encrypted/bonded, or subscribed notifies.
    const bool hidReady =
        info.isEncrypted() || info.isBonded() || (bleHid->getSubscribedCount() > 0);
    if (!hidReady) {
        connected = false;
        BLEConnected = false;
        return false;
    }

    connected = true;
    BLEConnected = true;
    return true;
#else
    return false;
#endif
}

int HidRemoteTransportSession::getBondCount() {
#if defined(CONFIG_BT_ENABLED)
    if (!NimBLEDevice::isInitialized()) return 0;
    return NimBLEDevice::getNumBonds();
#else
    return 0;
#endif
}

String HidRemoteTransportSession::getBondLabel(int index) {
#if defined(CONFIG_BT_ENABLED)
    if (!NimBLEDevice::isInitialized()) return "";
    const int n = NimBLEDevice::getNumBonds();
    if (index < 0 || index >= n) return "";
    return String(NimBLEDevice::getBondedAddress(index).toString().c_str());
#else
    (void)index;
    return "";
#endif
}

String HidRemoteTransportSession::getConnectedAddress() {
#if defined(CONFIG_BT_ENABLED)
    if (transport != HID_REMOTE_BLE) return "";
    if (!NimBLEDevice::isInitialized()) return "";
    NimBLEServer *server = NimBLEDevice::getServer();
    if (server == nullptr || server->getConnectedCount() == 0) return "";
    NimBLEConnInfo info = server->getPeerInfo(0);
    NimBLEAddress id = info.getIdAddress();
    std::string idStr = id.toString();
    if (!id.isNull() && idStr.size() > 0) return String(idStr.c_str());
    return String(info.getAddress().toString().c_str());
#else
    return "";
#endif
}

bool HidRemoteTransportSession::isConnectedToAddr(const String &addr) {
#if defined(CONFIG_BT_ENABLED)
    if (!isConnected() || addr.isEmpty()) return false;
    return connectionMatchesAddr(addr);
#else
    (void)addr;
    return false;
#endif
}

String HidRemoteTransportSession::displayNameForAddr(const String &addr) const {
    if (addr.isEmpty()) return "(unknown)";
    String alias = kvxConfig.getHidRemoteHostAlias(addr);
    if (alias.length()) return alias;
    if (addr.length() > 11) return addr.substring(addr.length() - 11);
    return addr;
}

bool HidRemoteTransportSession::isKnownHostAddress(const String &addr) const {
#if defined(CONFIG_BT_ENABLED)
    if (addr.isEmpty()) return false;
    if (kvxConfig.findHidRemoteHostSlotForAddr(addr) > 0) return true;
    if (!NimBLEDevice::isInitialized()) return false;
    const int n = NimBLEDevice::getNumBonds();
    for (int i = 0; i < n; i++) {
        if (String(NimBLEDevice::getBondedAddress(i).toString().c_str()).equalsIgnoreCase(addr)) {
            return true;
        }
    }
    return false;
#else
    (void)addr;
    return false;
#endif
}

void HidRemoteTransportSession::rememberConnectedHost() {
#if defined(CONFIG_BT_ENABLED)
    if (transport != HID_REMOTE_BLE) return;
    String addr = getConnectedAddress();
    if (addr.isEmpty()) return;

    kvxConfig.setHidRemotePreferredHost(addr);

    // Prefer fuzzy match so ID vs RPA does not create a duplicate slot
    int slot = findSlotMatchingConnection();
    if (slot > 0) {
        String prev = kvxConfig.getHidRemoteHostSlot(slot);
        if (!prev.equalsIgnoreCase(addr)) {
            // Keep canonical live address; move alias if needed
            String oldAlias = kvxConfig.getHidRemoteHostAlias(prev);
            kvxConfig.setHidRemoteHostSlot(slot, addr);
            if (oldAlias.length() && kvxConfig.getHidRemoteHostAlias(addr).isEmpty()) {
                kvxConfig.setHidRemoteHostAlias(addr, oldAlias);
            }
        }
    } else if (kvxConfig.findHidRemoteHostSlotForAddr(addr) == 0) {
        int empty = kvxConfig.findEmptyHidRemoteHostSlot();
        if (empty > 0) kvxConfig.setHidRemoteHostSlot(empty, addr);
    }

    // Default saved name: existing alias, else global host name, else Host + short MAC
    if (kvxConfig.getHidRemoteHostAlias(addr).length() == 0) {
        String name;
        if (kvxConfig.hidRemoteHostName.length() > 0) {
            name = kvxConfig.hidRemoteHostName;
        } else {
            String shortAddr = addr;
            int colons = 0;
            for (unsigned i = 0; i < shortAddr.length(); i++) {
                if (shortAddr[i] == ':') colons++;
            }
            if (colons >= 3 && shortAddr.length() >= 8) {
                name = "Host " + shortAddr.substring(shortAddr.length() - 8);
            } else {
                name = "Host " + shortAddr;
            }
        }
        kvxConfig.setHidRemoteHostAlias(addr, name);
    }
#else
    return;
#endif
}

void HidRemoteTransportSession::syncHostSlotsWithBonds() {
#if defined(CONFIG_BT_ENABLED)
    if (transport != HID_REMOTE_BLE) return;
    if (!NimBLEDevice::isInitialized()) return;

    // Do NOT wipe slots when a bond string does not match — ID vs RPA mismatches
    // were clearing remembered hosts across restarts. Slots are cleared only via Forget.
    // Map orphan bonds into free slots (and refresh slot addr to canonical bond string).
    const int n = NimBLEDevice::getNumBonds();
    for (int i = 0; i < n; i++) {
        String addr = String(NimBLEDevice::getBondedAddress(i).toString().c_str());
        if (addr.isEmpty()) continue;

        int existing = kvxConfig.findHidRemoteHostSlotForAddr(addr);
        if (existing > 0) continue;

        // Fuzzy: bond may match a live slot via connectionMatchesAddr-style compare later;
        // try case-insensitive already done. Leave existing slot strings intact.
        int empty = kvxConfig.findEmptyHidRemoteHostSlot();
        if (empty <= 0) break;
        kvxConfig.setHidRemoteHostSlot(empty, addr);
        if (kvxConfig.getHidRemoteHostAlias(addr).isEmpty()) {
            String shortAddr = addr;
            if (shortAddr.length() >= 8) {
                kvxConfig.setHidRemoteHostAlias(addr, "Host " + shortAddr.substring(shortAddr.length() - 8));
            } else {
                kvxConfig.setHidRemoteHostAlias(addr, "Host " + shortAddr);
            }
        }
    }
#endif
}

#if defined(CONFIG_BT_ENABLED)
static NimBLEAddress resolveBondAddress(const String &addr) {
    const int n = NimBLEDevice::getNumBonds();
    for (int i = 0; i < n; i++) {
        NimBLEAddress b = NimBLEDevice::getBondedAddress(i);
        if (String(b.toString().c_str()).equalsIgnoreCase(addr)) return b;
    }
    NimBLEAddress pub(std::string(addr.c_str()), BLE_ADDR_PUBLIC);
    if (!pub.isNull()) return pub;
    return NimBLEAddress(std::string(addr.c_str()), BLE_ADDR_RANDOM);
}

static void clearBleWhitelist() {
    while (NimBLEDevice::getWhiteListCount() > 0) {
        NimBLEDevice::whiteListRemove(NimBLEDevice::getWhiteListAddress(0));
    }
}
#endif

bool HidRemoteTransportSession::ensureAdvertising() {
#if defined(CONFIG_BT_ENABLED)
    if (transport != HID_REMOTE_BLE) return false;
    if (!NimBLEDevice::isInitialized()) return false;
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    if (adv == nullptr) return false;
    if (!adv->isAdvertising()) adv->start();
    return true;
#else
    return false;
#endif
}

bool HidRemoteTransportSession::advertiseOpen() {
#if defined(CONFIG_BT_ENABLED)
    if (transport != HID_REMOTE_BLE) return false;
    if (!NimBLEDevice::isInitialized()) return false;
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    if (adv == nullptr) return false;

    adv->stop();
    delay(50);
    clearBleWhitelist();
    adv->setScanFilter(false, false);
    buildHidAdvertisement(adv);

    bool ok = adv->start();
    if (!ok || !adv->isAdvertising()) {
        delay(50);
        ok = adv->start();
    }
    return ok || adv->isAdvertising();
#else
    return false;
#endif
}

bool HidRemoteTransportSession::advertiseForHost(const String &addr, bool whitelistOnly) {
#if defined(CONFIG_BT_ENABLED)
    if (transport != HID_REMOTE_BLE) return false;
    if (!NimBLEDevice::isInitialized()) return false;
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    if (adv == nullptr) return false;

    adv->stop();
    delay(50);
    clearBleWhitelist();

    // Resolve the real NimBLE bond entry (correct address type). Wrong type in the
    // whitelist silently blocks reconnects from phones using RPAs.
    bool haveBond = false;
    if (addr.length() > 0) {
        const int idx = bondIndexForStoredAddr(addr);
        if (idx >= 0) {
            NimBLEDevice::whiteListAdd(NimBLEDevice::getBondedAddress(idx));
            haveBond = true;
        } else {
            // Slot string may not match bond table textually — still try both types
            NimBLEAddress pub(std::string(addr.c_str()), BLE_ADDR_PUBLIC);
            NimBLEAddress rnd(std::string(addr.c_str()), BLE_ADDR_RANDOM);
            if (!pub.isNull()) NimBLEDevice::whiteListAdd(pub);
            if (!rnd.isNull()) NimBLEDevice::whiteListAdd(rnd);
            haveBond = NimBLEDevice::getWhiteListCount() > 0;
        }
    }

    // Always rebuild full HID ADV so the host can find/reconnect to the keyboard.
    buildHidAdvertisement(adv);

    // Prefer whitelist when we have a real bond; otherwise open ADV and filter in software.
    const bool useWl = whitelistOnly && haveBond && NimBLEDevice::getWhiteListCount() > 0;
    adv->setScanFilter(false, useWl);

    bool ok = adv->start();
    if (!ok || !adv->isAdvertising()) {
        delay(50);
        ok = adv->start();
    }
    // If whitelist advertising fails to stay up, fall back to open discoverable.
    if ((!ok && !adv->isAdvertising()) || (useWl && !adv->isAdvertising())) {
        clearBleWhitelist();
        adv->setScanFilter(false, false);
        buildHidAdvertisement(adv);
        ok = adv->start();
    }
    return ok || adv->isAdvertising();
#else
    (void)addr;
    (void)whitelistOnly;
    return false;
#endif
}

bool HidRemoteTransportSession::advertiseForAnyBonded() {
#if defined(CONFIG_BT_ENABLED)
    if (transport != HID_REMOTE_BLE) return false;
    if (!NimBLEDevice::isInitialized()) return false;
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    if (adv == nullptr) return false;

    adv->stop();
    delay(50);
    clearBleWhitelist();
    const int n = NimBLEDevice::getNumBonds();
    if (n <= 0) {
        return advertiseOpen();
    }
    for (int i = 0; i < n; i++) {
        NimBLEDevice::whiteListAdd(NimBLEDevice::getBondedAddress(i));
    }

    buildHidAdvertisement(adv);
    adv->setScanFilter(false, true);
    bool ok = adv->start();
    if (!ok || !adv->isAdvertising()) {
        delay(50);
        ok = adv->start();
    }
    return ok || adv->isAdvertising();
#else
    return false;
#endif
}

void HidRemoteTransportSession::refreshHostLabel() {
    hostLabel = "";
    if (!isConnected()) return;

#if defined(CONFIG_BT_ENABLED)
    if (transport == HID_REMOTE_BLE) {
        String addr = getConnectedAddress();
        String alias = kvxConfig.getHidRemoteHostAlias(addr);
        if (alias.length()) {
            hostLabel = alias;
            return;
        }
        if (kvxConfig.hidRemoteHostName.length() > 0) {
            hostLabel = kvxConfig.hidRemoteHostName;
            return;
        }
        if (addr.length()) {
            hostLabel = displayNameForAddr(addr);
            return;
        }
    }
#endif

    if (kvxConfig.hidRemoteHostName.length() > 0) {
        hostLabel = kvxConfig.hidRemoteHostName;
        return;
    }

    if (transport == HID_REMOTE_USB) hostLabel = "USB Host";
}

bool HidRemoteTransportSession::disconnectHost(bool readvertise) {
#if defined(CONFIG_BT_ENABLED)
    if (transport != HID_REMOTE_BLE) return false;
    if (!NimBLEDevice::isInitialized()) return false;
    NimBLEServer *server = NimBLEDevice::getServer();
    if (server) {
        unsigned long t0 = millis();
        while (server->getConnectedCount() > 0 && (millis() - t0) < 2500) {
            NimBLEConnInfo info = server->getPeerInfo(0);
            server->disconnect(info.getConnHandle());
            delay(80);
        }
    }
    if (bleHid != nullptr) bleHid->clearConnected();
    connected = false;
    BLEConnected = false;
    hostLabel = "";
    delay(300);
    if (readvertise) {
        advertiseOpen();
    }
    return true;
#else
    (void)readvertise;
    return false;
#endif
}

bool HidRemoteTransportSession::forgetBond(const String &addr) {
#if defined(CONFIG_BT_ENABLED)
    if (transport != HID_REMOTE_BLE) return false;
    if (!NimBLEDevice::isInitialized() || addr.isEmpty()) return false;

    // Drop live link if this host (or its RPA/identity form) is connected
    if (isConnected() && connectionMatchesAddr(addr)) {
        disconnectHost(false);
        delay(200);
    }

    bool deleted = false;
    const int idx = bondIndexForStoredAddr(addr);
    if (idx >= 0) {
        deleted = NimBLEDevice::deleteBond(NimBLEDevice::getBondedAddress(idx));
    }
    if (!deleted) {
        // Try both address types — deleteBond fails when type is wrong
        NimBLEAddress pub(std::string(addr.c_str()), BLE_ADDR_PUBLIC);
        NimBLEAddress rnd(std::string(addr.c_str()), BLE_ADDR_RANDOM);
        if (!pub.isNull()) deleted = NimBLEDevice::deleteBond(pub) || deleted;
        if (!rnd.isNull()) deleted = NimBLEDevice::deleteBond(rnd) || deleted;
    }
    // Last resort: scan bonds for a peer that string-matches ignoring type
    if (!deleted) {
        const int n = NimBLEDevice::getNumBonds();
        for (int i = n - 1; i >= 0; i--) {
            NimBLEAddress b = NimBLEDevice::getBondedAddress(i);
            if (bleAddrEqual(addr, String(b.toString().c_str()))) {
                deleted = NimBLEDevice::deleteBond(b) || deleted;
            }
        }
    }

    // Always clear slot/alias — host is forgotten from UI even if NVS unpair glitches
    kvxConfig.clearHidRemoteHostAlias(addr);
    int slot = kvxConfig.findHidRemoteHostSlotForAddr(addr);
    if (slot > 0) kvxConfig.clearHidRemoteHostSlot(slot);
    // Also clear any slot that fuzzy-matches via remaining bond identity
    for (int s = 1; s <= KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT; s++) {
        String slotted = kvxConfig.getHidRemoteHostSlot(s);
        if (slotted.length() && bleAddrEqual(slotted, addr)) {
            kvxConfig.clearHidRemoteHostSlot(s);
            kvxConfig.clearHidRemoteHostAlias(slotted);
        }
    }
    if (kvxConfig.hidRemotePreferredHost.equalsIgnoreCase(addr) ||
        (kvxConfig.hidRemotePreferredHost.length() &&
         bleAddrEqual(kvxConfig.hidRemotePreferredHost, addr))) {
        kvxConfig.setHidRemotePreferredHost("");
    }

    advertiseOpen();
    // Success if bond gone or we cleared the slot mapping
    return deleted || bondIndexForStoredAddr(addr) < 0 ||
           kvxConfig.findHidRemoteHostSlotForAddr(addr) == 0;
#else
    (void)addr;
    return false;
#endif
}

bool HidRemoteTransportSession::forgetBonds() {
#if defined(CONFIG_BT_ENABLED)
    if (transport != HID_REMOTE_BLE) return false;
    if (!NimBLEDevice::isInitialized()) return false;

    disconnectHost(false);

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    if (adv) adv->stop();
    delay(50);

    // Delete until empty (ble_gap_unpair can fail mid-list if called while connected)
    for (int attempt = 0; attempt < 3; attempt++) {
        int n = NimBLEDevice::getNumBonds();
        if (n <= 0) break;
        for (int i = n - 1; i >= 0; i--) {
            NimBLEAddress a = NimBLEDevice::getBondedAddress(i);
            NimBLEDevice::deleteBond(a);
            delay(20);
        }
        delay(50);
    }

    clearBleWhitelist();
    kvxConfig.hidRemoteHostAliases.clear();
    kvxConfig.clearAllHidRemoteHostSlots();
    kvxConfig.setHidRemotePreferredHost("");
    kvxConfig.saveFile();

    connected = false;
    BLEConnected = false;
    advertiseOpen();
    return NimBLEDevice::getNumBonds() == 0;
#else
    return false;
#endif
}

bool HidRemoteTransportSession::switchToHost(const String &addr, unsigned long timeoutMs) {
#if defined(CONFIG_BT_ENABLED)
    if (transport != HID_REMOTE_BLE) return false;
    if (addr.isEmpty()) return false;

    String cur = getConnectedAddress();
    if (isConnected() && (connectionMatchesAddr(addr) || cur.equalsIgnoreCase(addr))) {
        kvxConfig.setHidRemotePreferredHost(addr);
        refreshHostLabel();
        return true;
    }

    // Previous host will try to auto-reconnect — exclude its slot address
    String excludeAddr = "";
    if (isConnected()) {
        const int liveSlot = findSlotMatchingConnection();
        if (liveSlot > 0) excludeAddr = kvxConfig.getHidRemoteHostSlot(liveSlot);
        if (excludeAddr.isEmpty()) excludeAddr = getConnectedAddress();
    }

    disconnectHost(false);
    delay(300);
    kvxConfig.setHidRemotePreferredHost(addr);

    // Prefer whitelist for the target so the old phone cannot grab the link
    if (bondIndexForStoredAddr(addr) >= 0) {
        advertiseForHost(addr, true);
    } else {
        advertiseOpen();
    }

    unsigned long half = timeoutMs > 0 ? (timeoutMs / 2) : 10000;
    if (half < 8000) half = timeoutMs > 0 ? timeoutMs : 10000;

    if (!waitConnectedExpected(addr, half, excludeAddr)) {
        // Fallback: open ADV but still refuse the previous host
        advertiseOpen();
        unsigned long rest = timeoutMs > half ? (timeoutMs - half) : 8000;
        if (!waitConnectedExpected(addr, rest, excludeAddr)) {
            advertiseOpen();
            return false;
        }
    }
    return true;
#else
    (void)addr;
    (void)timeoutMs;
    return false;
#endif
}

bool HidRemoteTransportSession::switchToSlot(int slot1to8, unsigned long timeoutMs) {
#if defined(CONFIG_BT_ENABLED)
    String addr = kvxConfig.getHidRemoteHostSlot(slot1to8);
    if (addr.isEmpty()) return false;
    return switchToHost(addr, timeoutMs);
#else
    (void)slot1to8;
    (void)timeoutMs;
    return false;
#endif
}

bool HidRemoteTransportSession::pairIntoSlot(int slot1to8, unsigned long timeoutMs) {
#if defined(CONFIG_BT_ENABLED)
    if (transport != HID_REMOTE_BLE) return false;
    if (slot1to8 < 1 || slot1to8 > KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT) return false;
    if (bleHid == nullptr || !NimBLEDevice::isInitialized()) return false;

    disconnectHost(false);
    delay(200);
    clearBleWhitelist();

    if (!advertiseOpen()) return false;
    // Empty expectedAddr → accept only a host that is not already remembered
    if (!waitConnectedExpected(String(""), timeoutMs)) return false;

    String addr = getConnectedAddress();
    if (addr.isEmpty()) return false;

    int existing = kvxConfig.findHidRemoteHostSlotForAddr(addr);
    if (existing > 0 && existing != slot1to8) kvxConfig.clearHidRemoteHostSlot(existing);

    kvxConfig.setHidRemoteHostSlot(slot1to8, addr);
    kvxConfig.setHidRemotePreferredHost(addr);
    rememberConnectedHost();
    if (kvxConfig.findHidRemoteHostSlotForAddr(addr) != slot1to8) {
        int wrong = kvxConfig.findHidRemoteHostSlotForAddr(addr);
        if (wrong > 0) kvxConfig.clearHidRemoteHostSlot(wrong);
        kvxConfig.setHidRemoteHostSlot(slot1to8, addr);
    }
    refreshHostLabel();
    return true;
#else
    (void)slot1to8;
    (void)timeoutMs;
    return false;
#endif
}

bool HidRemoteTransportSession::reconnectNewHost() {
#if defined(CONFIG_BT_ENABLED)
    // Pair into the first empty host slot (or slot 1 if all full — overwrite not done; fail)
    int slot = kvxConfig.findEmptyHidRemoteHostSlot();
    if (slot <= 0) return false;
    return pairIntoSlot(slot, 0);
#else
    return false;
#endif
}

void HidRemoteTransportSession::pressKey(uint8_t key) {
    if (keyboardHid != nullptr) {
        keyboardHid->press(key);
        keyboardHid->releaseAll();
    }
}

void HidRemoteTransportSession::pressMedia(const MediaKeyReport &key) {
    if (keyboardHid != nullptr) {
        keyboardHid->press(key);
        keyboardHid->releaseAll();
    }
}

void HidRemoteTransportSession::releaseAll() {
    if (keyboardHid != nullptr) keyboardHid->releaseAll();
}

void HidRemoteTransportSession::mouseMove(int8_t x, int8_t y, int8_t wheel) {
    if (transport == HID_REMOTE_BLE && bleHid != nullptr) {
        bleHid->mouseMove(x, y, wheel);
        return;
    }
#if defined(USB_as_HID)
    if (usbMouse != nullptr) usbMouse->move(x, y, wheel);
#endif
}

void HidRemoteTransportSession::mouseClick(uint8_t button) {
    if (transport == HID_REMOTE_BLE && bleHid != nullptr) {
        bleHid->mouseClick(button);
        return;
    }
#if defined(USB_as_HID)
    if (usbMouse != nullptr) usbMouse->click(button);
#endif
}
