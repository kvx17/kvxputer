// uncomment the following line to use NimBLE library
// #define USE_NIMBLE

#ifndef ESP32_BLE_KEYBOARD_H
#define ESP32_BLE_KEYBOARD_H
#include "sdkconfig.h"
#if defined(CONFIG_BT_ENABLED) && !defined(KVX_NO_NIMBLE)
#define USE_NIMBLE
#include "NimBLECharacteristic.h"
#include "NimBLEHIDDevice.h"
#include "NimBLEAdvertising.h"
#include "NimBLEServer.h"
#define BLEDevice NimBLEDevice
#define BLEServerCallbacks NimBLEServerCallbacks
#define BLECharacteristicCallbacks NimBLECharacteristicCallbacks
#define BLEHIDDevice NimBLEHIDDevice
#define BLECharacteristic NimBLECharacteristic
#define BLEAdvertising NimBLEAdvertising
#define BLEServer NimBLEServer

#include "Bad_Usb_Lib.h"
#include "Print.h"
#include "keys.h"

#define BLE_KEYBOARD_VERSION "0.0.4"
#define BLE_KEYBOARD_VERSION_MAJOR 0
#define BLE_KEYBOARD_VERSION_MINOR 0
#define BLE_KEYBOARD_VERSION_REVISION 4

class BleKeyboard : public BLEServerCallbacks, public BLECharacteristicCallbacks, public HIDInterface {
private:
    BLEHIDDevice *hid;
    BLECharacteristic *inputKeyboard;
    BLECharacteristic *outputKeyboard;
    BLECharacteristic *inputMediaKeys;
    BLECharacteristic *inputMouse;
    BLEAdvertising *advertising;
    NimBLEConnInfo *activeConnection = nullptr;
    NimBLEServer *pServer;
    KeyReport _keyReport;
    MediaKeyReport _mediaKeyReport;
    uint8_t _mouseButtons = 0;
    String deviceName;
    String deviceManufacturer;
    uint8_t batteryLevel;
    bool connected = false;
    uint32_t _delay_ms = 7;
    void delay_ms(uint64_t ms);

    uint16_t vid = 0x0000;
    uint16_t pid = 0x0001;
    uint16_t version = 0x0100;
    // Generic HID appearance (not a vendor-branded keyboard)
    uint16_t appearance = 0x03C0;

    const uint8_t *_asciimap;

public:
    BleKeyboard(
        String deviceName = "Keyboard", String deviceManufacturer = "HID",
        uint8_t batteryLevel = 100
    );
    void begin(const uint8_t *layout = KeyboardLayout_en_US) override { begin(layout, HID_KEYBOARD); };
    void begin(const uint8_t *layout, uint16_t showAs);
    void setLayout(const uint8_t *layout = KeyboardLayout_en_US) { _asciimap = layout; }
    void end(void) override;
    void sendReport(KeyReport *keys);
    void sendReport(MediaKeyReport *keys);
    void sendMouseReport(int8_t x, int8_t y, int8_t wheel = 0);
    void mouseMove(int8_t x, int8_t y, int8_t wheel = 0);
    void mouseClick(uint8_t buttons);
    void mouseRelease(uint8_t buttons);
    size_t press(uint8_t k) override;
    size_t pressRaw(uint8_t k) override;
    size_t press(const MediaKeyReport k);
    size_t release(uint8_t k) override;
    size_t releaseRaw(uint8_t k) override;
    size_t release(const MediaKeyReport k);
    size_t write(uint8_t c) override;
    size_t write(const MediaKeyReport c);
    size_t write(const uint8_t *buffer, size_t size) override;
    void releaseAll(void) override;
    bool isConnected(void);
    void clearConnected(void) {
        connected = false;
        m_subCount = 0;
    }
    void setBatteryLevel(uint8_t level);
    void setName(const String &deviceName);
    void setDelay(uint32_t ms);
    void setAppearence(uint16_t v) { appearance = v; }
    void setRandomUUID(void) { _randUUID = !_randUUID; };
    bool getRandomUUID() { return _randUUID; };
    uint16_t getAppearence() { return appearance; }

    void set_vendor_id(uint16_t vid);
    void set_product_id(uint16_t pid);
    void set_version(uint16_t version);
    uint8_t getSubscribedCount() { return m_subCount; }
    // Bonded reconnects sometimes encrypt without onSubscribe / auth callbacks.
    // Seed so sendReport and session-ready checks are not stuck at sub=0.
    void ensureNotifyReady() {
        connected = true;
        if (m_subCount == 0) m_subCount = 1;
    }

protected:
    bool _randUUID = false;

    class ServerCallbacks : public NimBLEServerCallbacks {
    private:
        BleKeyboard *parent;

    public:
        ServerCallbacks(BleKeyboard *kb) : parent(kb) {}
        void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override;
        void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override;
        void onAuthenticationComplete(NimBLEConnInfo &connInfo) override;
    };
    class CharacteristicCallbacks : public NimBLECharacteristicCallbacks {
    private:
        BleKeyboard *parent;

    public:
        CharacteristicCallbacks(BleKeyboard *kb) : parent(kb) {}
        void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override;
        void onSubscribe(
            NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo, uint16_t subValue
        ) override;
    };

private:
    uint8_t m_subCount{0};
};

#endif // CONFIG_BT_ENABLED && !KVX_NO_NIMBLE

#if defined(KVX_NO_NIMBLE)
#include "Bad_Usb_Lib.h"
#include "KeyboardLayout.h"
#include "Print.h"
#include "keys.h"
// Stub when NimBLE is unavailable (e.g. ESP32-P4 Tab5 milestone 1).
class BleKeyboard : public HIDInterface {
public:
    BleKeyboard(String = "Keyboard", String = "HID", uint8_t = 100) {}
    void begin(const uint8_t *layout = KeyboardLayout_en_US) override { (void)layout; }
    void begin(const uint8_t *layout, uint16_t) { (void)layout; }
    void setLayout(const uint8_t *layout = KeyboardLayout_en_US) { (void)layout; }
    void end(void) override {}
    void sendReport(KeyReport *) {}
    void sendReport(MediaKeyReport *) {}
    void sendMouseReport(int8_t = 0, int8_t = 0, int8_t = 0) {}
    void mouseMove(int8_t = 0, int8_t = 0, int8_t = 0) {}
    void mouseClick(uint8_t) {}
    void mouseRelease(uint8_t) {}
    size_t press(uint8_t) override { return 0; }
    size_t pressRaw(uint8_t) override { return 0; }
    size_t press(const MediaKeyReport) { return 0; }
    size_t release(uint8_t) override { return 0; }
    size_t releaseRaw(uint8_t) override { return 0; }
    size_t release(const MediaKeyReport) { return 0; }
    size_t write(uint8_t) override { return 0; }
    size_t write(const MediaKeyReport) { return 0; }
    size_t write(const uint8_t *, size_t) override { return 0; }
    void releaseAll(void) override {}
    bool isConnected(void) { return false; }
    void clearConnected(void) {}
    void setBatteryLevel(uint8_t) {}
    void setName(const String &) {}
    void setDelay(uint32_t) {}
    void setAppearence(uint16_t) {}
    void setRandomUUID(void) {}
    bool getRandomUUID() { return false; }
    uint16_t getAppearence() { return 0; }
    void set_vendor_id(uint16_t) {}
    void set_product_id(uint16_t) {}
    void set_version(uint16_t) {}
    uint8_t getSubscribedCount() { return 0; }
    void ensureNotifyReady() {}
};
#endif // KVX_NO_NIMBLE

#endif // ESP32_BLE_KEYBOARD_H
