#include "hid_remote_transport.h"
#include "menu/others/badusb_ble/ducky_typer.h"
#include "root/hal/radio_mem.h"
#include "root/ui/display.h"
#include <KeyboardLayout.h>
#include <NimBLEDevice.h>
#include <esp_mac.h>
#include <USB.h>
#if defined(USB_as_HID)
#include "tusb.h"
#endif
#include <globals.h>

HidRemoteTransportSession gHidRemoteSession;

static void setHidRemoteBleMac() {
    static const uint8_t mac[6] = {0x88, 0x3B, 0x5F, 0x2D, 0x71, 0x58};
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
        if (!s.mouseActive) USB.begin();
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
        if (!s.keyboardActive) USB.begin();
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
    safeCleanupDuckyBLE(hid_ble);
    setHidRemoteBleMac();

    String deviceName = kvxConfig.hidRemoteBleName;
    if (deviceName.isEmpty()) deviceName = "kvxputer HID";

    if (!NimBLEDevice::isInitialized()) {
        NimBLEDevice::init(std::string(deviceName.c_str()));
    } else if (NimBLEDevice::getAdvertising()) {
        NimBLEDevice::getAdvertising()->stop();
    }

    if (s.bleHid == nullptr) {
        s.bleHid = new BleCompositeHid(deviceName, "kvxputer", 100);
    }
    s.bleHid->setName(deviceName);
    const uint8_t *layout = KeyboardLayout_en_US;
    s.bleHid->begin(layout);
    s.bleHid->setDelay(kvxConfig.badUSBBLEKeyDelay);
    s.keyboardHid = s.bleHid;
    s.keyboardActive = true;
    s.mouseActive = true;
    hid_ble = s.bleHid;
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
        safeCleanupDuckyBLE(hid_ble);
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
        return ok;
    }

    if (t == HID_REMOTE_BLE) {
        return ensureBle(*this);
    }

    return false;
}

void HidRemoteTransportSession::end() {
    if (transport == HID_REMOTE_USB) teardownUsb(*this);
    else teardownBle(*this);
    connected = false;
}

bool HidRemoteTransportSession::waitConnected(unsigned long timeoutMs) {
    if (transport == HID_REMOTE_USB) {
        connected = keyboardActive || mouseActive;
        return connected;
    }
#if defined(CONFIG_BT_ENABLED)
    unsigned long start = millis();
    while (!check(EscPress)) {
        if (bleHid != nullptr && bleHid->isConnected()) {
            BLEConnected = true;
            connected = true;
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
