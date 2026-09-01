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
    if (s.bleHid != nullptr && s.bleHid->isConnected()) {
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
#if !defined(LITE_VERSION)
    safeCleanupDuckyBLE(hid_ble);
#endif
    setHidRemoteBleMac();

    String deviceName = kvxConfig.hidRemoteBleName;
    if (deviceName.isEmpty()) deviceName = KVXKEYBOARD_HID_NAME;

    if (!NimBLEDevice::isInitialized()) {
        NimBLEDevice::init(std::string(deviceName.c_str()));
    } else if (NimBLEDevice::getAdvertising()) {
        NimBLEDevice::getAdvertising()->stop();
    }

    if (s.bleHid == nullptr) {
        s.bleHid = new BleCompositeHid(deviceName, "HID", 100);
    }
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
    BLEConnected = false;
    return true;
#else
    (void)s;
    return false;
#endif
}

static void teardownBle(HidRemoteTransportSession &s) {
#if defined(CONFIG_BT_ENABLED)
    if (s.bleHid != nullptr) {
#if !defined(LITE_VERSION)
        safeCleanupDuckyBLE(hid_ble);
#endif
        delete s.bleHid;
        s.bleHid = nullptr;
    }
    s.keyboardHid = nullptr;
    s.keyboardActive = false;
    s.mouseActive = false;
    BLEConnected = false;
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
        if (bleHid != nullptr && bleHid->isConnected() && bleHid->getSubscribedCount() > 0) {
            BLEConnected = true;
            connected = true;
            refreshHostLabel();
            return true;
        }
        if (timeoutMs > 0 && (millis() - start) >= timeoutMs) break;
        delay(50);
    }
#endif
    return false;
}

bool HidRemoteTransportSession::isConnected() {
    if (transport == HID_REMOTE_USB) return connected;
#if defined(CONFIG_BT_ENABLED)
    return bleHid != nullptr && bleHid->isConnected();
#else
    return false;
#endif
}

void HidRemoteTransportSession::refreshHostLabel() {
    hostLabel = "";
    if (!isConnected()) return;

    if (kvxConfig.hidRemoteHostName.length() > 0) {
        hostLabel = kvxConfig.hidRemoteHostName;
        return;
    }

#if defined(CONFIG_BT_ENABLED)
    if (transport == HID_REMOTE_BLE && NimBLEDevice::isInitialized()) {
        NimBLEServer *server = NimBLEDevice::getServer();
        if (server != nullptr && server->getConnectedCount() > 0) {
            hostLabel = String(server->getPeerInfo(0).getAddress().toString().c_str());
            return;
        }
    }
#endif

    if (transport == HID_REMOTE_USB) hostLabel = "USB Host";
}

bool HidRemoteTransportSession::forgetBonds() {
#if defined(CONFIG_BT_ENABLED)
    if (transport != HID_REMOTE_BLE) return false;
    if (!NimBLEDevice::isInitialized()) return false;
    NimBLEServer *server = NimBLEDevice::getServer();
    if (server) {
        while (server->getConnectedCount() > 0) {
            NimBLEConnInfo info = server->getPeerInfo(0);
            server->disconnect(info.getConnHandle());
            delay(40);
        }
    }
    NimBLEDevice::deleteAllBonds();
    if (NimBLEDevice::getAdvertising()) NimBLEDevice::getAdvertising()->start();
    connected = false;
    BLEConnected = false;
    return true;
#else
    return false;
#endif
}

bool HidRemoteTransportSession::reconnectNewHost() {
#if defined(CONFIG_BT_ENABLED)
    if (NimBLEDevice::isInitialized()) NimBLEDevice::deleteAllBonds();
    gRandomHidMac = true;
    end();
    if (!begin(
            HID_REMOTE_BLE,
            static_cast<HidRemoteCapability>(HID_CAP_KEYBOARD | HID_CAP_MEDIA | HID_CAP_MOUSE)
        )) {
        return false;
    }
    return waitConnected();
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
