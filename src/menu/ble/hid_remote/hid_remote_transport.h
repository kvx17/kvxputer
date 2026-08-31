#pragma once

#include "hid_remote_config.h"
#include <BleCompositeHid.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>

class HidRemoteTransportSession {
public:
    HidRemoteTransport transport = HID_REMOTE_USB;
    bool connected = false;
    bool keyboardActive = false;
    bool mouseActive = false;

    HIDInterface *keyboardHid = nullptr;
    BleCompositeHid *bleHid = nullptr;
    USBHIDKeyboard *usbKeyboard = nullptr;
    USBHIDMouse *usbMouse = nullptr;

    bool begin(HidRemoteTransport t, HidRemoteCapability caps);
    void end();
    bool waitConnected(unsigned long timeoutMs = 0);
    bool isConnected();

    void pressKey(uint8_t key);
    void pressMedia(const MediaKeyReport &key);
    void releaseAll();
    void mouseMove(int8_t x, int8_t y, int8_t wheel = 0);
    void mouseClick(uint8_t button);
};

extern HidRemoteTransportSession gHidRemoteSession;
