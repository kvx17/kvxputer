#include "hid_remote_transport.h"
#include "menu/others/badusb_ble/ducky_typer.h"
#include "root/hal/radio_mem.h"
#include "root/ui/display.h"
#include <KeyboardLayout.h>
#include <NimBLEDevice.h>
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

    // Open discoverable advertising (scan response helps phones find the name)
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    if (adv) {
        adv->stop();
        adv->setScanFilter(false, false);
        adv->enableScanResponse(true);
        adv->setConnectableMode(BLE_GAP_CONN_MODE_UND);
        adv->start();
    }
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

bool HidRemoteTransportSession::waitConnected(unsigned long timeoutMs) {
    if (transport == HID_REMOTE_USB) {
        connected = keyboardActive || mouseActive;
        return connected;
    }
#if defined(CONFIG_BT_ENABLED)
    unsigned long start = millis();
    while (!check(EscPress)) {
        if (isConnected()) {
            BLEConnected = true;
            connected = true;
            rememberConnectedHost();
            refreshHostLabel();
            return true;
        }
        if (timeoutMs > 0 && (millis() - start) >= timeoutMs) break;
        delay(50);
    }
    connected = false;
    BLEConnected = false;
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
    if (transport != HID_REMOTE_BLE || !isConnected()) return "";
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

String HidRemoteTransportSession::displayNameForAddr(const String &addr) const {
    if (addr.isEmpty()) return "(unknown)";
    String alias = kvxConfig.getHidRemoteHostAlias(addr);
    if (alias.length()) return alias;
    if (addr.length() > 11) return addr.substring(addr.length() - 11);
    return addr;
}

void HidRemoteTransportSession::rememberConnectedHost() {
#if defined(CONFIG_BT_ENABLED)
    if (transport != HID_REMOTE_BLE) return;
    String addr = getConnectedAddress();
    if (addr.isEmpty()) return;

    kvxConfig.setHidRemotePreferredHost(addr);

    // Default saved name: existing alias, else global host name, else Host + short MAC
    if (kvxConfig.getHidRemoteHostAlias(addr).length() == 0) {
        String name;
        if (kvxConfig.hidRemoteHostName.length() > 0) {
            name = kvxConfig.hidRemoteHostName;
        } else {
            String shortAddr = addr;
            // Prefer trailing octets: "aa:bb:cc:dd:ee:ff" -> "dd:ee:ff"
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
    adv->enableScanResponse(true);
    adv->setDiscoverableMode(BLE_GAP_DISC_MODE_GEN);
    adv->setConnectableMode(BLE_GAP_CONN_MODE_UND);

    // Refresh advertised name so phones can find it
    String deviceName = kvxConfig.hidRemoteBleName;
    if (deviceName.isEmpty()) deviceName = KVXKEYBOARD_HID_NAME;
    adv->setName(std::string(deviceName.c_str()));
    NimBLEDevice::setDeviceName(std::string(deviceName.c_str()));

    bool ok = adv->start();
    // Kick once more if start reported already-active/false
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
    delay(30);
    clearBleWhitelist();

    if (addr.length() > 0) {
        NimBLEDevice::whiteListAdd(resolveBondAddress(addr));
    }

    const bool useWl = whitelistOnly && NimBLEDevice::getWhiteListCount() > 0;
    adv->setScanFilter(false, useWl);
    adv->enableScanResponse(true);
    adv->setConnectableMode(BLE_GAP_CONN_MODE_UND);
    return adv->start();
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
    delay(30);
    clearBleWhitelist();
    const int n = NimBLEDevice::getNumBonds();
    if (n <= 0) {
        return advertiseOpen();
    }
    for (int i = 0; i < n; i++) {
        NimBLEDevice::whiteListAdd(NimBLEDevice::getBondedAddress(i));
    }

    adv->setScanFilter(false, true);
    adv->enableScanResponse(true);
    adv->setConnectableMode(BLE_GAP_CONN_MODE_UND);
    return adv->start();
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
        while (server->getConnectedCount() > 0) {
            NimBLEConnInfo info = server->getPeerInfo(0);
            server->disconnect(info.getConnHandle());
            delay(50);
        }
    }
    if (bleHid != nullptr) bleHid->clearConnected();
    connected = false;
    BLEConnected = false;
    hostLabel = "";
    delay(150);
    if (readvertise) {
        if (kvxConfig.hidRemotePreferredHost.length() > 0) {
            advertiseForHost(kvxConfig.hidRemotePreferredHost, true);
        } else if (getBondCount() > 0) {
            advertiseForAnyBonded();
        } else {
            advertiseOpen();
        }
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

    String connectedAddr = getConnectedAddress();
    if (connectedAddr.length() && connectedAddr.equalsIgnoreCase(addr)) {
        disconnectHost(false);
    }

    NimBLEAddress peer = resolveBondAddress(addr);
    bool ok = NimBLEDevice::deleteBond(peer);
    kvxConfig.clearHidRemoteHostAlias(addr);
    if (kvxConfig.hidRemotePreferredHost.equalsIgnoreCase(addr)) {
        kvxConfig.setHidRemotePreferredHost("");
    }
    if (getBondCount() > 0) advertiseForAnyBonded();
    else advertiseOpen();
    return ok;
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
    if (cur.length() && cur.equalsIgnoreCase(addr) && isConnected()) {
        kvxConfig.setHidRemotePreferredHost(addr);
        refreshHostLabel();
        return true;
    }

    disconnectHost(false);
    kvxConfig.setHidRemotePreferredHost(addr);
    advertiseForHost(addr, true);
    if (!waitConnected(timeoutMs)) {
        advertiseForAnyBonded();
        return false;
    }
    return true;
#else
    (void)addr;
    (void)timeoutMs;
    return false;
#endif
}

bool HidRemoteTransportSession::reconnectNewHost() {
#if defined(CONFIG_BT_ENABLED)
    // Safe path: do NOT tear down the BLE stack (that was crashing).
    // Disconnect current link, open discoverable advertising, wait for a pair.
    if (transport != HID_REMOTE_BLE) return false;
    if (bleHid == nullptr || !NimBLEDevice::isInitialized()) return false;

    disconnectHost(false);
    delay(200);

    kvxConfig.setHidRemotePreferredHost("");
    clearBleWhitelist();

    if (!advertiseOpen()) return false;
    return waitConnected(0);
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
