#ifndef __PAHUB_H__
#define __PAHUB_H__

#include "root/config/config.h"
#include <Arduino.h>

static constexpr uint8_t PAHUB_CH_COUNT = 6;
static constexpr uint8_t PAHUB_ADDR_MIN = 0x70;
static constexpr uint8_t PAHUB_ADDR_MAX = 0x77;
static constexpr uint8_t PAHUB_RFID2_ADDR = 0x28;
static constexpr uint8_t PAHUB_NFC_ADDR = 0x24;
static constexpr uint8_t PAHUB_SCROLL_ADDR = 0x40;
static constexpr uint8_t PAHUB_JOY2_ADDR = 0x63;

struct PahubScanResult {
    uint8_t addrs[16] = {};
    uint8_t count = 0;
    PahubDevice fingerprint = PahubDevNone;
};

const char *pahubDeviceName(PahubDevice dev);
PahubDevice pahubFingerprintAddr(uint8_t addr);
String pahubScanHint(uint8_t ch);

bool pahubEnabled();
bool pahubIsConnected();
bool pahubPresent();
bool pahubReconnect();
bool pahubBootProbe();
void pahubInitInputDevices();
String pahubStatusLabel();
bool grovePortABusy();
bool pahubSelectRf433r();
bool pahubDeselectRf433r();
bool pahubDeviceOnMux(PahubDevice dev);

int8_t pahubChannelFor(PahubDevice dev);
int8_t pahubChannelForRfidModule();
bool pahubSelect(int8_t ch);
bool pahubDeselect();
bool pahubScanChannel(uint8_t ch, PahubScanResult &out);
bool pahubScanAll(PahubScanResult out[PAHUB_CH_COUNT]);
// Scan PaHub channels and assign the first matching device. Returns channel or -1.
int8_t pahubDiscoverDevice(PahubDevice dev);

// Hold mux mutex across scroll + joystick polls so channel selects do not interleave.
void pahubBeginGrovePoll();
void pahubEndGrovePoll();
bool pahubGrovePollActive();

// Blocking session lock: select channel (if any), hold mutex until destroyed.
// No-op when PaHub is off or channel < 0. Move-only.
class PahubChannelGuard {
public:
    explicit PahubChannelGuard(int8_t channel);
    static PahubChannelGuard forRfid();
    static PahubChannelGuard forDevice(PahubDevice dev);
    ~PahubChannelGuard();
    PahubChannelGuard(const PahubChannelGuard &) = delete;
    PahubChannelGuard &operator=(const PahubChannelGuard &) = delete;
    PahubChannelGuard(PahubChannelGuard &&o) noexcept;
    PahubChannelGuard &operator=(PahubChannelGuard &&o) noexcept;
    bool active() const { return _selected; }

private:
    bool _held = false;
    bool _selected = false;
};

// Non-blocking poll lock for encoder-task Grove devices. If PaHub is off or the
// device has no channel, ok() is true with no mutex. If RFID holds the mux, ok()
// is false and the caller should skip. A failed channel select still sets ok()
// so a leftover assignment can fall back to a direct PORT.A probe.
class PahubTryGuard {
public:
    explicit PahubTryGuard(PahubDevice dev);
    ~PahubTryGuard();
    PahubTryGuard(const PahubTryGuard &) = delete;
    PahubTryGuard &operator=(const PahubTryGuard &) = delete;
    bool ok() const { return _ok; }

private:
    bool _ok = false;
    bool _held = false;
    bool _selected = false;
};

#endif
