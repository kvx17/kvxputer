#pragma once

#include "hid_remote_config.h"
#include <BleCompositeHid.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>
#include <vector>

class HidRemoteTransportSession {
public:
    HidRemoteTransport transport = HID_REMOTE_USB;
    bool connected = false;
    bool keyboardActive = false;
    bool mouseActive = false;
    String hostLabel = "";

    HIDInterface *keyboardHid = nullptr;
    BleCompositeHid *bleHid = nullptr;
    USBHIDKeyboard *usbKeyboard = nullptr;
    USBHIDMouse *usbMouse = nullptr;

    bool begin(HidRemoteTransport t, HidRemoteCapability caps);
    void end();
    bool waitConnected(unsigned long timeoutMs = 0);
    bool isConnected();
    void refreshHostLabel();
    const String &getHostLabel() const { return hostLabel; }
    int getBondCount();
    String getBondLabel(int index = 0);
    String getConnectedAddress();
    bool isConnectedToAddr(const String &addr);
    String displayNameForAddr(const String &addr) const;
    bool isKnownHostAddress(const String &addr) const;
    // preferSlot 1..8 pins the host to that slot (pair/switch). 0 = infer.
    // neverCreateSlot: update existing only (reconnect) — avoids duplicate slots.
    void rememberConnectedHost(int preferSlot = 0, bool neverCreateSlot = false);
    void syncHostSlotsWithBonds();
    void dedupeHostSlots();
    // Human-readable slot/bond mapping for the diagnostics screen.
    std::vector<String> describeHostBinding();
    int bondIndexForSlot(int slot1to8);
    int bondIndexForLiveHost();
    bool ensureAdvertising();
    bool advertiseStop();
    bool advertiseOpen();
    // Open discoverable ADV tuned for cross-OS rediscovery (software-filter required).
    bool advertiseReconnect();
    // Low-duty directed ADV to one bonded host (blocks other centrals at LL).
    bool advertiseDirectedForHost(const String &addr);
    // Whitelist-only undirected ADV for one remembered host (fallback path).
    bool advertiseForHost(const String &addr, bool whitelistOnly = true);
    bool advertiseForAnyBonded();
    bool disconnectHost(bool readvertise = false);
    bool forgetBond(const String &addr);
    bool forgetBonds();
    // expectedAddr empty = accept only a new (not remembered) host
    // excludeAddr = always reject this peer (e.g. previous host when switching slots)
    bool waitConnectedExpected(
        const String &expectedAddr, unsigned long timeoutMs = 0, const String &excludeAddr = String("")
    );
    bool switchToHost(const String &addr, unsigned long timeoutMs = 30000);
    bool switchToSlot(int slot1to8, unsigned long timeoutMs = 30000);
    bool pairIntoSlot(int slot1to8, unsigned long timeoutMs = 0);
    bool reconnectNewHost();

    void pressKey(uint8_t key);
    void pressMedia(const MediaKeyReport &key);
    void releaseAll();
    void mouseMove(int8_t x, int8_t y, int8_t wheel = 0);
    void mouseClick(uint8_t button);
};

extern HidRemoteTransportSession gHidRemoteSession;
