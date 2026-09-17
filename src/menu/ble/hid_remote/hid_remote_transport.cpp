#include "hid_remote_transport.h"
#include "hid_remote_ui.h"
#include "menu/others/badusb_ble/ducky_typer.h"
#include "root/hal/radio_mem.h"
#include "root/ui/display.h"
#include "root/config/config.h"

#if HID_SLOT_DEBUG
#define HID_SLOT_LOG(fmt, ...) Serial.printf("HID_SLOT " fmt "\n", ##__VA_ARGS__)
#else
#define HID_SLOT_LOG(...)
#endif
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

    // BleKeyboard::begin() starts connectable ADV — stop so the slot screen
    // does not invite every bonded host before the user picks one.
    s.advertiseStop();
    return true;
#else
    (void)s;
    return false;
#endif
}

static void teardownBle(HidRemoteTransportSession &s) {
#if defined(CONFIG_BT_ENABLED)
    // Drop the link before BleKeyboard::end()/deinit — a stuck peer used to
    // spin forever in end()'s disconnect loop and freeze the UI on Esc.
    if (NimBLEDevice::isInitialized()) {
        NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
        if (adv != nullptr) adv->stop();
        NimBLEServer *server = NimBLEDevice::getServer();
        if (server != nullptr) {
            unsigned long t0 = millis();
            while (server->getConnectedCount() > 0 && (millis() - t0) < 2000) {
                server->disconnect(server->getPeerInfo(0).getConnHandle());
                delay(80);
            }
        }
        delay(100);
    }

    BleCompositeHid *p = s.bleHid;
    s.bleHid = nullptr;
    s.keyboardHid = nullptr;
    s.keyboardActive = false;
    s.mouseActive = false;
    s.connected = false;
    BLEConnected = false;
    s.hostLabel = "";
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
static bool bleAddrEqual(const String &a, const String &b) { return hidRemoteAddrEqual(a, b); }

static String bleAddrCore(const String &addr) { return hidRemoteAddrCore(addr); }

// NimBLEAddress(string) runs std::stoull on any 17-char input; with exceptions off
// a malformed stored address aborts the firmware. Validate before constructing.
static bool makeBleAddr(const String &addr, uint8_t type, NimBLEAddress &out) {
    const String core = bleAddrCore(addr);
    if (core.length() != 17) return false;
    for (int i = 0; i < 17; i++) {
        const char c = core[i];
        if ((i % 3) == 2) {
            if (c != ':') return false;
        } else if (!isxdigit((unsigned char)c)) {
            return false;
        }
    }
    out = NimBLEAddress(std::string(core.c_str()), type);
    return !out.isNull();
}

static int bondIndexForStoredAddr(const String &addr) {
    if (addr.isEmpty() || !NimBLEDevice::isInitialized()) return -1;
    const int n = NimBLEDevice::getNumBonds();
    const String core = bleAddrCore(addr);
    for (int i = 0; i < n; i++) {
        NimBLEAddress b = NimBLEDevice::getBondedAddress(i);
        String bonded = String(b.toString().c_str());
        if (bleAddrEqual(addr, bonded) || bleAddrCore(bonded) == core) return i;
    }
    // Fall back: construct both address types and compare to bond objects
    NimBLEAddress pub, rnd;
    const bool havePub = makeBleAddr(core, BLE_ADDR_PUBLIC, pub);
    const bool haveRnd = makeBleAddr(core, BLE_ADDR_RANDOM, rnd);
    for (int i = 0; i < n; i++) {
        NimBLEAddress b = NimBLEDevice::getBondedAddress(i);
        if ((havePub && b == pub) || (haveRnd && b == rnd)) return i;
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

    // Last resort: core string of expected vs peer/id
    const String expCore = bleAddrCore(expected);
    if (expCore.length() && (expCore == bleAddrCore(peer) || expCore == bleAddrCore(id))) return true;
    return false;
}

static int findSlotMatchingConnection() {
    for (int slot = 1; slot <= KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT; slot++) {
        String slotted = kvxConfig.getHidRemoteHostSlot(slot);
        if (slotted.length() && connectionMatchesAddr(slotted)) return slot;
    }
    return 0;
}

static bool bleHidLinkReady() {
    if (!NimBLEDevice::isInitialized()) return false;
    NimBLEServer *server = NimBLEDevice::getServer();
    if (server == nullptr || server->getConnectedCount() == 0) return false;
    NimBLEConnInfo info = server->getPeerInfo(0);
    if (info.isEncrypted() || info.isBonded()) return true;
    return gHidRemoteSession.bleHid != nullptr && gHidRemoteSession.bleHid->getSubscribedCount() > 0;
}

static bool buildHidAdvertisement(NimBLEAdvertising *adv, bool fastIntervals = false) {
    if (adv == nullptr) return false;
    String deviceName = kvxConfig.hidRemoteBleName;
    if (deviceName.isEmpty()) deviceName = KVXKEYBOARD_HID_NAME;
    NimBLEDevice::setDeviceName(std::string(deviceName.c_str()));

    NimBLEAdvertisementData advData;
    // General discoverable + BR/EDR not supported (Linux/Windows prefer this)
    advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
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
    // Units are 0.625 ms. Faster intervals help Windows/Android rediscovery.
    if (fastIntervals) {
        adv->setMinInterval(32);  // 20 ms
        adv->setMaxInterval(48);  // 30 ms
    } else {
        adv->setMinInterval(80);  // 50 ms
        adv->setMaxInterval(160); // 100 ms
    }
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
    const bool exclusiveHost = !acceptAny && !acceptNewOnly && expectedAddr.length() > 0;
    unsigned long start = millis();
    unsigned long lastAdvKick = 0;
    unsigned long lastAdvRefresh = 0;
    unsigned long quietUntil = 0;
    unsigned long quietMs = 2000;
    unsigned long peerSeenAt = 0;
    unsigned long phaseStart = millis();
    bool openPhase = false;
    bool resumeAfterQuiet = false;
    // Resolve exclude / expected to bond indices up front — more reliable than address
    // strings across iOS/Android RPA and Windows/Linux public addresses.
    const int excludeBond = excludeAddr.length() ? bondIndexForStoredAddr(excludeAddr) : -1;
    const int expectedBond = exclusiveHost ? bondIndexForStoredAddr(expectedAddr) : -1;
    const int wantSlot =
        exclusiveHost ? kvxConfig.findHidRemoteHostSlotForAddr(expectedAddr) : 0;

    // Hosts parked in the OTHER filled slots are the ones that must never win.
    // Deriving this from slots (not "every bond except expected") keeps the target
    // connectable when its stored address no longer resolves to a bond index.
    String otherSlotAddrs[KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT];
    int otherSlotBonds[KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT];
    int otherSlotCount = 0;
    if (exclusiveHost) {
        for (int s = 1; s <= KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT; s++) {
            if (wantSlot > 0 && s == wantSlot) continue;
            String slotted = kvxConfig.getHidRemoteHostSlot(s);
            if (slotted.isEmpty()) continue;
            if (hidRemoteAddrEqual(slotted, expectedAddr)) continue;
            const int bond = bondIndexForStoredAddr(slotted);
            if (bond >= 0 && expectedBond >= 0 && bond == expectedBond) continue;
            otherSlotAddrs[otherSlotCount] = slotted;
            otherSlotBonds[otherSlotCount] = bond;
            otherSlotCount++;
        }
    }

    String priorBonds[KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT];
    int priorBondCount = 0;
    if (acceptNewOnly && NimBLEDevice::isInitialized()) {
        const int n = NimBLEDevice::getNumBonds();
        for (int i = 0; i < n && priorBondCount < KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT; i++) {
            priorBonds[priorBondCount++] =
                String(NimBLEDevice::getBondedAddress(i).toString().c_str());
        }
    }

    // Live peer belongs to a different remembered slot.
    auto isOtherSlotPeer = [&](int liveBond) -> bool {
        for (int i = 0; i < otherSlotCount; i++) {
            if (liveBond >= 0 && otherSlotBonds[i] == liveBond) return true;
            if (connectionMatchesAddr(otherSlotAddrs[i])) return true;
        }
        return false;
    };

    auto isPriorKnownPeer = [&]() -> bool {
        for (int i = 0; i < priorBondCount; i++) {
            if (connectionMatchesAddr(priorBonds[i])) return true;
        }
        for (int slot = 1; slot <= KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT; slot++) {
            String slotted = kvxConfig.getHidRemoteHostSlot(slot);
            if (slotted.length() && connectionMatchesAddr(slotted)) return true;
        }
        return false;
    };

    auto resumeAdv = [&]() {
        if (millis() < quietUntil) return;
        if (acceptNewOnly) {
            advertiseOpen();
            return;
        }
        if (!exclusiveHost) return;

        // Exclusive reconnect must NEVER use open undirected ADV while another
        // bonded central (iOS) exists — phones race in and starve Linux/BlueZ.
        //
        // ESP32-S3 link controller supports low-duty directed advertising and
        // LE Privacy 1.2 (datasheet §4.3.3.2). Directed ADV addresses InitA at
        // the selected bond only, so other bonded hosts cannot complete a link
        // at the controller. Whitelist undirected is the fallback when directed
        // cannot start (no bond / stack reject).
        const unsigned long phaseLen = 12000;
        if ((millis() - phaseStart) > phaseLen) {
            openPhase = !openPhase;
            phaseStart = millis();
        }

        if (!openPhase) {
            if (advertiseDirectedForHost(expectedAddr)) return;
            // Directed failed (no bond address) — try filter-accept list next.
            openPhase = true;
            phaseStart = millis();
        }
        if (advertiseForHost(expectedAddr, true)) return;

        // Stay dark rather than opening ADV to every bonded phone.
        HID_SLOT_LOG("resumeAdv quiet — no directed/wl for '%s'", expectedAddr.c_str());
        advertiseStop();
    };

    auto rejectPeer = [&](const char *why) {
        NimBLEServer *server = NimBLEDevice::getServer();
        String peer = "";
        String id = "";
        const int liveBond = bondIndexForConnection();
        bool wasExcluded = false;
        if (server != nullptr && server->getConnectedCount() > 0) {
            NimBLEConnInfo info = server->getPeerInfo(0);
            peer = String(info.getAddress().toString().c_str());
            id = String(info.getIdAddress().toString().c_str());
            wasExcluded =
                (excludeBond >= 0 && liveBond == excludeBond) ||
                isOtherSlotPeer(liveBond) ||
                (!excludeAddr.isEmpty() && connectionMatchesAddr(excludeAddr));
        }
        HID_SLOT_LOG(
            "reject %s peer=%s id=%s bond=%d expectBond=%d excludeBond=%d",
            why,
            peer.c_str(),
            id.c_str(),
            liveBond,
            expectedBond,
            excludeBond
        );
        hidRemoteLedSet(HID_REMOTE_LED_REJECT);
        disconnectHost(false);
        peerSeenAt = 0;
        // Stay dark long enough that iOS backs off and the selected host can connect.
        unsigned long gap = wasExcluded ? (quietMs + 1500) : quietMs;
        if (gap < 3000) gap = 3000;
        if (gap > 6000) gap = 6000;
        quietUntil = millis() + gap;
        resumeAfterQuiet = true;
        if (quietMs < 5000) {
            quietMs += 500;
            if (quietMs > 5000) quietMs = 5000;
        }
        // Resume on directed ADV so the rejected host cannot re-enter.
        openPhase = false;
        phaseStart = millis();
        lastAdvKick = millis();
        lastAdvRefresh = millis();
    };

    hidRemoteLedSet(acceptNewOnly ? HID_REMOTE_LED_PAIRING : HID_REMOTE_LED_CONNECTING);
    HID_SLOT_LOG(
        "wait expect='%s' new=%d any=%d excl=%d exclude='%s' expectBond=%d excludeBond=%d prior=%d",
        expectedAddr.c_str(),
        (int)acceptNewOnly,
        (int)acceptAny,
        (int)exclusiveHost,
        excludeAddr.c_str(),
        expectedBond,
        excludeBond,
        priorBondCount
    );

    if (acceptNewOnly || exclusiveHost) {
        resumeAdv();
        lastAdvKick = millis();
        lastAdvRefresh = millis();
    }

    while (!check(EscPress)) {
        hidRemoteLedTick();
        NimBLEServer *server = NimBLEDevice::isInitialized() ? NimBLEDevice::getServer() : nullptr;
        const int gapLinks = (server != nullptr) ? (int)server->getConnectedCount() : 0;

        if (gapLinks > 0) {
            if (peerSeenAt == 0) peerSeenAt = millis();
            const bool hidReady = bleHidLinkReady();
            bool knownWrong = false;
            bool knownRight = false;
            const int liveBond = bondIndexForConnection();

            if ((excludeBond >= 0 && liveBond == excludeBond) ||
                (!excludeAddr.isEmpty() && connectionMatchesAddr(excludeAddr))) {
                knownWrong = true;
            }

            if (acceptAny) {
                if (hidReady) knownRight = true;
            } else if (acceptNewOnly) {
                // New peers create a NimBLE bond during handshake. Do NOT treat
                // "now bonded" as known — only reject hosts that were already
                // bonded/slotted before this pair wait started.
                if (isPriorKnownPeer()) knownWrong = true;
                else if (hidReady) knownRight = true;
            } else {
                // Exclusive switch: only the selected host may stay connected.
                const int liveSlot = findSlotMatchingConnection();
                const bool addrMatch = connectionMatchesAddr(expectedAddr);
                const bool bondMatch =
                    (expectedBond >= 0 && liveBond >= 0 && expectedBond == liveBond);
                const bool slotMatch = (wantSlot > 0 && liveSlot == wantSlot);
                const bool positiveMatch = addrMatch || bondMatch || slotMatch;

                const bool excluded =
                    (excludeBond >= 0 && liveBond >= 0 && liveBond == excludeBond) ||
                    (!excludeAddr.isEmpty() && connectionMatchesAddr(excludeAddr));

                if (positiveMatch && !isOtherSlotPeer(liveBond)) {
                    // The selected host: wait as long as needed for the HID link.
                    knownRight = true;
                } else if (excluded || isOtherSlotPeer(liveBond) ||
                           (liveSlot > 0 && wantSlot > 0 && liveSlot != wantSlot)) {
                    knownWrong = true;
                } else if (expectedBond >= 0 && liveBond >= 0 && liveBond != expectedBond) {
                    // Known bond, but not the one we asked for.
                    knownWrong = true;
                } else if (hidReady && expectedBond < 0 && liveBond < 0 &&
                           !isOtherSlotPeer(liveBond)) {
                    // Target's stored address no longer resolves to a bond, and
                    // the live peer is not a known bond either. Accept and
                    // re-canonicalize — but never when another slot's host linked.
                    knownRight = true;
                } else if ((millis() - peerSeenAt) > 8000) {
                    // Unidentified for too long — let the next window try again.
                    knownWrong = true;
                }
            }

            if (knownWrong) {
                rejectPeer("wrong-host");
                delay(40);
                continue;
            }

            if (knownRight && hidReady) {
                BLEConnected = true;
                connected = true;
                rememberConnectedHost(wantSlot > 0 ? wantSlot : 0, true);
                advertiseStop();
                refreshHostLabel();
                hidRemoteLedSet(HID_REMOTE_LED_CONNECTED);
                HID_SLOT_LOG(
                    "accept addr=%s slot=%d bond=%d",
                    getConnectedAddress().c_str(),
                    findSlotMatchingConnection(),
                    bondIndexForConnection()
                );
                return true;
            }

            // Right host still handshaking — keep waiting.
            delay(40);
            continue;
        }

        peerSeenAt = 0;

        if (millis() >= quietUntil) {
            NimBLEAdvertising *adv =
                NimBLEDevice::isInitialized() ? NimBLEDevice::getAdvertising() : nullptr;
            if (adv != nullptr) {
                const bool needStart = resumeAfterQuiet || !adv->isAdvertising();
                const bool needRefresh = exclusiveHost && (millis() - lastAdvRefresh) > 5000;
                if (needStart || needRefresh) {
                    resumeAdv();
                    resumeAfterQuiet = false;
                    lastAdvKick = millis();
                    lastAdvRefresh = millis();
                }
            }
        }

        if (timeoutMs > 0 && (millis() - start) >= timeoutMs) break;
        delay(40);
    }
    disconnectHost(false);
    advertiseStop();
    connected = false;
    BLEConnected = false;
    hidRemoteLedSet(HID_REMOTE_LED_DISCONNECTED);
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
        if (hidRemoteAddrEqual(String(NimBLEDevice::getBondedAddress(i).toString().c_str()), addr)) {
            return true;
        }
    }
    return false;
#else
    (void)addr;
    return false;
#endif
}

void HidRemoteTransportSession::dedupeHostSlots() {
#if defined(CONFIG_BT_ENABLED)
    if (transport != HID_REMOTE_BLE) return;

    // Collapse slots that point at the same bond / same address core.
    for (int a = 1; a <= KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT; a++) {
        String addrA = kvxConfig.getHidRemoteHostSlot(a);
        if (addrA.isEmpty()) continue;
        const int bondA =
            NimBLEDevice::isInitialized() ? bondIndexForStoredAddr(addrA) : -1;
        for (int b = a + 1; b <= KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT; b++) {
            String addrB = kvxConfig.getHidRemoteHostSlot(b);
            if (addrB.isEmpty()) continue;
            bool same = hidRemoteAddrEqual(addrA, addrB);
            if (!same && bondA >= 0 && NimBLEDevice::isInitialized()) {
                same = (bondIndexForStoredAddr(addrB) == bondA);
            }
            if (!same) continue;

            // Keep slot a; migrate alias from b if needed, then clear b.
            String aliasB = kvxConfig.getHidRemoteHostAlias(addrB);
            if (aliasB.length() && kvxConfig.getHidRemoteHostAlias(addrA).isEmpty()) {
                kvxConfig.setHidRemoteHostAlias(addrA, aliasB);
            }
            kvxConfig.clearHidRemoteHostAlias(addrB);
            kvxConfig.clearHidRemoteHostSlot(b);
            HID_SLOT_LOG("dedupe cleared slot %d (same as %d)", b, a);
        }
    }
#endif
}

void HidRemoteTransportSession::rememberConnectedHost(int preferSlot, bool neverCreateSlot) {
#if defined(CONFIG_BT_ENABLED)
    if (transport != HID_REMOTE_BLE) return;
    String addr = getConnectedAddress();
    if (addr.isEmpty()) return;

    String canonical = addr;
    const int bc = bondIndexForConnection();
    if (bc >= 0) {
        canonical = String(NimBLEDevice::getBondedAddress(bc).toString().c_str());
        if (canonical.isEmpty()) canonical = addr;
    }

    kvxConfig.setHidRemotePreferredHost(canonical);

    int slot = 0;
    if (preferSlot >= 1 && preferSlot <= KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT) {
        slot = preferSlot;
    }
    // Prefer slot that already owns this bond index — never overwrite a different
    // host's slot when ID/RPA matching is ambiguous.
    if (slot <= 0 && bc >= 0) {
        for (int s = 1; s <= KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT; s++) {
            String slotted = kvxConfig.getHidRemoteHostSlot(s);
            if (slotted.length() && bondIndexForStoredAddr(slotted) == bc) {
                slot = s;
                break;
            }
        }
    }
    if (slot <= 0) slot = findSlotMatchingConnection();
    if (slot <= 0) slot = kvxConfig.findHidRemoteHostSlotForAddr(canonical);
    if (slot <= 0) slot = kvxConfig.findHidRemoteHostSlotForAddr(addr);

    if (slot > 0) {
        String prev = kvxConfig.getHidRemoteHostSlot(slot);
        if (prev.isEmpty() || !hidRemoteAddrEqual(prev, canonical)) {
            String oldAlias = prev.length() ? kvxConfig.getHidRemoteHostAlias(prev) : String("");
            kvxConfig.setHidRemoteHostSlot(slot, canonical);
            if (oldAlias.length() && kvxConfig.getHidRemoteHostAlias(canonical).isEmpty()) {
                kvxConfig.setHidRemoteHostAlias(canonical, oldAlias);
            }
        } else if (!prev.equalsIgnoreCase(canonical)) {
            kvxConfig.setHidRemoteHostSlot(slot, canonical);
        }
    } else if (!neverCreateSlot) {
        // Only create a new slot from explicit pair paths — never from reconnect.
        int empty = kvxConfig.findEmptyHidRemoteHostSlot();
        if (empty > 0) {
            kvxConfig.setHidRemoteHostSlot(empty, canonical);
            slot = empty;
        }
    }

    // Clear any duplicate slots that now point at the same host/bond.
    dedupeHostSlots();

    addr = canonical;

    // Default saved name only when unset — do not overwrite manual names.
    if (kvxConfig.getHidRemoteHostAlias(addr).length() == 0) {
        String name;
        if (kvxConfig.hidRemoteHostName.length() > 0) {
            name = kvxConfig.hidRemoteHostName;
        } else {
            String shortAddr = addr;
            if (shortAddr.length() >= 8) {
                name = "Host " + shortAddr.substring(shortAddr.length() - 8);
            } else {
                name = "Host " + shortAddr;
            }
        }
        kvxConfig.setHidRemoteHostAlias(addr, name);
    }
#else
    (void)preferSlot;
    (void)neverCreateSlot;
#endif
}

int HidRemoteTransportSession::bondIndexForSlot(int slot1to8) {
#if defined(CONFIG_BT_ENABLED)
    if (!NimBLEDevice::isInitialized()) return -1;
    String addr = kvxConfig.getHidRemoteHostSlot(slot1to8);
    if (addr.isEmpty()) return -1;
    return bondIndexForStoredAddr(addr);
#else
    (void)slot1to8;
    return -1;
#endif
}

int HidRemoteTransportSession::bondIndexForLiveHost() {
#if defined(CONFIG_BT_ENABLED)
    return bondIndexForConnection();
#else
    return -1;
#endif
}

std::vector<String> HidRemoteTransportSession::describeHostBinding() {
    std::vector<String> lines;
#if defined(CONFIG_BT_ENABLED)
    if (!NimBLEDevice::isInitialized()) {
        lines.push_back("BLE not started");
        return lines;
    }

    const int bonds = NimBLEDevice::getNumBonds();
    lines.push_back("Bonds stored: " + String(bonds));
    for (int i = 0; i < bonds; i++) {
        lines.push_back(
            "b" + String(i) + " " + String(NimBLEDevice::getBondedAddress(i).toString().c_str())
        );
    }

    for (int s = 1; s <= KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT; s++) {
        String addr = kvxConfig.getHidRemoteHostSlot(s);
        if (addr.isEmpty()) continue;
        const int bond = bondIndexForStoredAddr(addr);
        lines.push_back(
            "s" + String(s) + " " + displayNameForAddr(addr) + (bond >= 0 ? " b" + String(bond) : " b-")
        );
        lines.push_back("   " + addr);
    }

    if (isConnected()) {
        lines.push_back("live " + getConnectedAddress());
        lines.push_back("live bond b" + String(bondIndexForConnection()));
        lines.push_back("live slot s" + String(findSlotMatchingConnection()));
    } else {
        lines.push_back("live: none");
    }
    lines.push_back("pref " + kvxConfig.hidRemotePreferredHost);
#else
    lines.push_back("BLE unavailable");
#endif
    return lines;
}

void HidRemoteTransportSession::syncHostSlotsWithBonds() {
#if defined(CONFIG_BT_ENABLED)
    if (transport != HID_REMOTE_BLE) return;
    if (!NimBLEDevice::isInitialized()) return;

    dedupeHostSlots();

    // Do NOT wipe slots when a bond string does not match — ID vs RPA mismatches
    // were clearing remembered hosts across restarts. Slots are cleared only via Forget.
    // Map orphan bonds into free slots (and refresh slot addr to canonical bond string).
    const int n = NimBLEDevice::getNumBonds();
    for (int i = 0; i < n; i++) {
        String addr = String(NimBLEDevice::getBondedAddress(i).toString().c_str());
        if (addr.isEmpty()) continue;

        int existing = kvxConfig.findHidRemoteHostSlotForAddr(addr);
        if (existing > 0) continue;

        // Same bond already slotted under a different address string — refresh that slot
        bool alreadySlotted = false;
        for (int s = 1; s <= KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT; s++) {
            String slotted = kvxConfig.getHidRemoteHostSlot(s);
            if (slotted.isEmpty()) continue;
            if (bondIndexForStoredAddr(slotted) == i) {
                if (!hidRemoteAddrEqual(slotted, addr)) {
                    String oldAlias = kvxConfig.getHidRemoteHostAlias(slotted);
                    kvxConfig.setHidRemoteHostSlot(s, addr);
                    if (oldAlias.length() && kvxConfig.getHidRemoteHostAlias(addr).isEmpty()) {
                        kvxConfig.setHidRemoteHostAlias(addr, oldAlias);
                    }
                }
                alreadySlotted = true;
                break;
            }
        }
        if (alreadySlotted) continue;

        // Also skip if any slot's stored address resolves to this same bond index
        // via core-string equality after refresh above.
        for (int s = 1; s <= KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT; s++) {
            String slotted = kvxConfig.getHidRemoteHostSlot(s);
            if (slotted.isEmpty()) continue;
            if (bondIndexForStoredAddr(slotted) == i) {
                alreadySlotted = true;
                break;
            }
        }
        if (alreadySlotted) continue;

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

    dedupeHostSlots();
#endif
}

#if defined(CONFIG_BT_ENABLED)
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

bool HidRemoteTransportSession::advertiseStop() {
#if defined(CONFIG_BT_ENABLED)
    if (transport != HID_REMOTE_BLE) return false;
    if (!NimBLEDevice::isInitialized()) return false;
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    if (adv == nullptr) return false;
    adv->stop();
    delay(50);
    clearBleWhitelist();
    adv->setScanFilter(false, false);
    adv->setConnectableMode(BLE_GAP_CONN_MODE_NON);
    HID_SLOT_LOG("adv stop");
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
    buildHidAdvertisement(adv, false);

    bool ok = adv->start();
    if (!ok || !adv->isAdvertising()) {
        delay(50);
        ok = adv->start();
    }
    HID_SLOT_LOG("adv open ok=%d", (int)(ok || adv->isAdvertising()));
    return ok || adv->isAdvertising();
#else
    return false;
#endif
}

bool HidRemoteTransportSession::advertiseReconnect() {
#if defined(CONFIG_BT_ENABLED)
    if (transport != HID_REMOTE_BLE) return false;
    if (!NimBLEDevice::isInitialized()) return false;
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    if (adv == nullptr) return false;

    adv->stop();
    delay(40);
    clearBleWhitelist();
    adv->setScanFilter(false, false);
    // Fast intervals + full HID payload: works for iOS, Android, Windows, Linux.
    buildHidAdvertisement(adv, true);

    bool ok = adv->start();
    if (!ok || !adv->isAdvertising()) {
        delay(60);
        ok = adv->start();
    }
    if (!ok || !adv->isAdvertising()) {
        delay(60);
        ok = adv->start();
    }
    HID_SLOT_LOG("adv reconnect ok=%d", (int)(ok || adv->isAdvertising()));
    return ok || adv->isAdvertising();
#else
    return false;
#endif
}

bool HidRemoteTransportSession::advertiseDirectedForHost(const String &addr) {
#if defined(CONFIG_BT_ENABLED)
    if (transport != HID_REMOTE_BLE) return false;
    if (!NimBLEDevice::isInitialized() || addr.isEmpty()) return false;
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    if (adv == nullptr) return false;

    const int idx = bondIndexForStoredAddr(addr);
    if (idx < 0) {
        HID_SLOT_LOG("adv directed '%s' no bond", addr.c_str());
        return false;
    }
    NimBLEAddress peer = NimBLEDevice::getBondedAddress(idx);
    if (peer.isNull()) return false;

    adv->stop();
    delay(50);
    clearBleWhitelist();
    adv->setScanFilter(false, false);
    // Legacy directed ADV cannot use scan response; leave prior ADV payload alone
    // (clearData() would make start() push an empty buffer).
    adv->enableScanResponse(false);
    adv->setDiscoverableMode(BLE_GAP_DISC_MODE_NON);
    adv->setConnectableMode(BLE_GAP_CONN_MODE_DIR);
    // Non-zero intervals select low-duty directed (not HD's 1.28 s limit).
    adv->setMinInterval(32);  // 20 ms
    adv->setMaxInterval(96);  // 60 ms

    bool ok = adv->start(0, &peer);
    if (!ok || !adv->isAdvertising()) {
        delay(60);
        ok = adv->start(0, &peer);
    }
    HID_SLOT_LOG(
        "adv directed '%s' peer=%s type=%u ok=%d",
        addr.c_str(),
        peer.toString().c_str(),
        (unsigned)peer.getType(),
        (int)(ok || adv->isAdvertising())
    );
    return ok || adv->isAdvertising();
#else
    (void)addr;
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

    bool haveBond = false;
    if (addr.length() > 0) {
        const int idx = bondIndexForStoredAddr(addr);
        if (idx >= 0) {
            // Exact bonded identity only — do not also open the filter to other hosts.
            NimBLEDevice::whiteListAdd(NimBLEDevice::getBondedAddress(idx));
            haveBond = true;
        } else {
            NimBLEAddress pub, rnd;
            if (makeBleAddr(addr, BLE_ADDR_PUBLIC, pub)) NimBLEDevice::whiteListAdd(pub);
            if (makeBleAddr(addr, BLE_ADDR_RANDOM, rnd)) NimBLEDevice::whiteListAdd(rnd);
            haveBond = NimBLEDevice::getWhiteListCount() > 0;
        }
    }

    if (!haveBond || NimBLEDevice::getWhiteListCount() == 0) {
        HID_SLOT_LOG("adv host '%s' no whitelist — stay stopped", addr.c_str());
        adv->setScanFilter(false, false);
        adv->setConnectableMode(BLE_GAP_CONN_MODE_NON);
        (void)whitelistOnly;
        return false;
    }

    buildHidAdvertisement(adv, true);
    // Connect requests only from the whitelisted host (selected slot).
    adv->setScanFilter(false, true);

    bool ok = adv->start();
    if (!ok || !adv->isAdvertising()) {
        delay(80);
        ok = adv->start();
    }
    if (!ok || !adv->isAdvertising()) {
        delay(80);
        ok = adv->start();
    }
    HID_SLOT_LOG(
        "adv host '%s' wl=%u ok=%d",
        addr.c_str(),
        (unsigned)NimBLEDevice::getWhiteListCount(),
        (int)(ok || adv->isAdvertising())
    );
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

    // Stop advertising FIRST so iOS/Android cannot race back in while we disconnect.
    advertiseStop();

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
    delay(200);
    // Keep ADV off unless caller explicitly wants open pairing again.
    advertiseStop();
    if (readvertise) advertiseOpen();
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

    // ble_gap_unpair() edits the resolving list and terminates links. Doing that
    // while advertising (especially with a filter-accept list) aborts the host.
    // Always go quiet and drop every link before touching bonds.
    advertiseStop();
    clearBleWhitelist();
    NimBLEServer *server = NimBLEDevice::getServer();
    if (server != nullptr && server->getConnectedCount() > 0) {
        disconnectHost(false);
        delay(250);
    }

    bool deleted = false;
    const int idx = bondIndexForStoredAddr(addr);
    if (idx >= 0) {
        NimBLEAddress bonded = NimBLEDevice::getBondedAddress(idx);
        if (!bonded.isNull()) deleted = NimBLEDevice::deleteBond(bonded);
    }
    if (!deleted) {
        // Try both address types — deleteBond fails when type is wrong
        NimBLEAddress pub, rnd;
        if (makeBleAddr(addr, BLE_ADDR_PUBLIC, pub)) deleted = NimBLEDevice::deleteBond(pub) || deleted;
        if (makeBleAddr(addr, BLE_ADDR_RANDOM, rnd)) deleted = NimBLEDevice::deleteBond(rnd) || deleted;
    }
    // Last resort: scan bonds for a peer that string-matches ignoring type
    if (!deleted) {
        const int n = NimBLEDevice::getNumBonds();
        for (int i = n - 1; i >= 0; i--) {
            NimBLEAddress b = NimBLEDevice::getBondedAddress(i);
            if (b.isNull()) continue;
            if (bleAddrEqual(addr, String(b.toString().c_str()))) {
                deleted = NimBLEDevice::deleteBond(b) || deleted;
                delay(20);
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

    advertiseStop();
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
    advertiseStop();
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
        const int slot = kvxConfig.findHidRemoteHostSlotForAddr(addr);
        rememberConnectedHost(slot > 0 ? slot : 0, true);
        refreshHostLabel();
        advertiseStop();
        return true;
    }

    // Previous live host (usually iOS) will hammer reconnect — exclude by canonical bond.
    String excludeAddr = "";
    if (isConnected()) {
        const int liveBond = bondIndexForConnection();
        if (liveBond >= 0) {
            excludeAddr = String(NimBLEDevice::getBondedAddress(liveBond).toString().c_str());
        }
        if (excludeAddr.isEmpty()) {
            const int liveSlot = findSlotMatchingConnection();
            if (liveSlot > 0) excludeAddr = kvxConfig.getHidRemoteHostSlot(liveSlot);
        }
        if (excludeAddr.isEmpty()) excludeAddr = getConnectedAddress();
    } else {
        // Not currently linked: still exclude the last preferred host when it is
        // a *different* slot than the target (stops iOS auto-winning Linux switch).
        String pref = kvxConfig.hidRemotePreferredHost;
        if (pref.length() && !hidRemoteAddrEqual(pref, addr)) {
            const int prefSlot = kvxConfig.findHidRemoteHostSlotForAddr(pref);
            const int wantSlot = kvxConfig.findHidRemoteHostSlotForAddr(addr);
            const int prefBond = bondIndexForStoredAddr(pref);
            const int wantBond = bondIndexForStoredAddr(addr);
            if (prefSlot > 0 && wantSlot > 0 && prefSlot != wantSlot) excludeAddr = pref;
            else if (prefBond >= 0 && wantBond >= 0 && prefBond != wantBond) excludeAddr = pref;
        }
    }

    // Drop current link and keep ADV off so iPhone cannot race back during settle.
    disconnectHost(false);
    delay(800);
    kvxConfig.setHidRemotePreferredHost(addr);

    // Exclusive wait: whitelist-only for this host; all other bonds are rejected.
    HID_SLOT_LOG(
        "switchToHost '%s' timeout=%lu exclude='%s'",
        addr.c_str(),
        (unsigned long)timeoutMs,
        excludeAddr.c_str()
    );
    if (!waitConnectedExpected(addr, timeoutMs, excludeAddr)) {
        advertiseStop();
        return false;
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
    HID_SLOT_LOG("pairIntoSlot %d", slot1to8);
    if (!waitConnectedExpected(String(""), timeoutMs)) {
        advertiseStop();
        return false;
    }

    String addr = getConnectedAddress();
    if (addr.isEmpty()) return false;

    int existing = kvxConfig.findHidRemoteHostSlotForAddr(addr);
    if (existing > 0 && existing != slot1to8) kvxConfig.clearHidRemoteHostSlot(existing);

    kvxConfig.setHidRemoteHostSlot(slot1to8, addr);
    kvxConfig.setHidRemotePreferredHost(addr);
    rememberConnectedHost(slot1to8, true);
    if (kvxConfig.findHidRemoteHostSlotForAddr(addr) != slot1to8) {
        int wrong = kvxConfig.findHidRemoteHostSlotForAddr(addr);
        if (wrong > 0 && wrong != slot1to8) kvxConfig.clearHidRemoteHostSlot(wrong);
        kvxConfig.setHidRemoteHostSlot(slot1to8, addr);
    }
    dedupeHostSlots();
    advertiseStop();
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
