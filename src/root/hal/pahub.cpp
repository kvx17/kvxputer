#include "root/hal/pahub.h"
#include "root/config/configPins.h"
#include "root/hal/bus_HAL.h"
#include "root/input/unit_scroll.h"
#include "root/input/unit_joystick2.h"
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <globals.h>

namespace {
bool g_connected = false;
String g_hints[PAHUB_CH_COUNT];
int grovePollDepth = 0;
bool grovePollMutexHeld = false;

SemaphoreHandle_t muxMutex() {
    static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    return mutex;
}

int8_t groveSda() {
    if (kvxConfigPins.i2c_bus.sda >= 0) return (int8_t)kvxConfigPins.i2c_bus.sda;
#ifdef GROVE_SDA
    return (int8_t)GROVE_SDA;
#else
    return 2;
#endif
}

int8_t groveScl() {
    if (kvxConfigPins.i2c_bus.scl >= 0) return (int8_t)kvxConfigPins.i2c_bus.scl;
#ifdef GROVE_SCL
    return (int8_t)GROVE_SCL;
#else
    return 1;
#endif
}

TwoWire *acquireGroveI2C() {
    TwoWire *w = acquireI2CBus(groveSda(), groveScl());
    if (w != nullptr) w->setClock(100000);
    return w;
}

bool writeControl(uint8_t mask) {
    TwoWire *w = acquireGroveI2C();
    if (w == nullptr) return false;
    w->beginTransmission(kvxConfig.pahubAddr);
    w->write(mask);
    int err = w->endTransmission();
    if (err != 0) {
        w->beginTransmission(kvxConfig.pahubAddr);
        w->write((uint8_t)0x00);
        w->endTransmission();
        w->beginTransmission(kvxConfig.pahubAddr);
        w->write(mask);
        err = w->endTransmission();
    }
    return err == 0;
}

bool probeHubAt(uint8_t addr) {
    TwoWire *w = acquireGroveI2C();
    if (w == nullptr) {
        g_connected = false;
        return false;
    }
    w->beginTransmission(addr);
    int err = w->endTransmission();
    return err == 0;
}

uint8_t discoverHubAddr() {
    if (probeHubAt(kvxConfig.pahubAddr)) return kvxConfig.pahubAddr;
    for (uint8_t addr = PAHUB_ADDR_MIN; addr <= PAHUB_ADDR_MAX; addr++) {
        if (addr == kvxConfig.pahubAddr) continue;
        if (probeHubAt(addr)) return addr;
    }
    return 0;
}

bool selectLocked(int8_t ch) {
    if (ch < 0 || ch >= (int8_t)PAHUB_CH_COUNT) return false;
    return writeControl((uint8_t)(1 << ch));
}

void takeMutex() { xSemaphoreTake(muxMutex(), portMAX_DELAY); }
bool tryMutex() { return xSemaphoreTake(muxMutex(), 0) == pdTRUE; }
bool takeMutexMs(uint32_t ms) { return xSemaphoreTake(muxMutex(), pdMS_TO_TICKS(ms)) == pdTRUE; }
void giveMutex() { xSemaphoreGive(muxMutex()); }

bool probeAndMaybeSaveAddr() {
    uint8_t found = discoverHubAddr();
    g_connected = (found != 0);
    if (found != 0 && found != kvxConfig.pahubAddr) kvxConfig.pahubAddr = found;
    return g_connected;
}
} // namespace

const char *pahubDeviceName(PahubDevice dev) {
    switch (dev) {
        case PahubDevRFID2: return "RFID2";
        case PahubDevNFC: return "NFC";
        case PahubDevScroll: return "Scroll";
        case PahubDevJoystick2: return "Joy2";
        case PahubDevRF433R: return "RF433R";
        case PahubDevNone:
        default: return "None";
    }
}

PahubDevice pahubFingerprintAddr(uint8_t addr) {
    if (addr == PAHUB_RFID2_ADDR) return PahubDevRFID2;
    if (addr == PAHUB_NFC_ADDR) return PahubDevNFC;
    if (addr == PAHUB_SCROLL_ADDR) return PahubDevScroll;
    if (addr == PAHUB_JOY2_ADDR) return PahubDevJoystick2;
    return PahubDevNone;
}

String pahubScanHint(uint8_t ch) {
    if (ch >= PAHUB_CH_COUNT) return "";
    return g_hints[ch];
}

bool pahubEnabled() { return kvxConfig.pahubEnabled; }

bool pahubIsConnected() { return kvxConfig.pahubEnabled && g_connected; }

bool pahubPresent() {
    takeMutex();
    bool ok = probeAndMaybeSaveAddr();
    giveMutex();
    return ok;
}

bool pahubReconnect() {
    uint8_t prevAddr = kvxConfig.pahubAddr;
    takeMutex();
    bool ok = probeAndMaybeSaveAddr();
    giveMutex();
    if (ok && kvxConfig.pahubAddr != prevAddr) kvxConfig.saveFile();
    return ok;
}

bool pahubBootProbe() {
    if (!kvxConfig.pahubEnabled) {
        g_connected = false;
        return false;
    }
    holdI2CBus(groveSda(), groveScl());
    return pahubPresent();
}

void pahubInitInputDevices() {
#if defined(UNIT_SCROLL)
    if (kvxConfig.unitScrollEnabled) unitScrollReconnect();
#endif
#if defined(UNIT_JOYSTICK2)
    unitJoystick2Reconnect();
#endif
}

bool pahubDeviceOnMux(PahubDevice dev) {
    return kvxConfig.pahubEnabled && pahubChannelFor(dev) >= 0;
}

String pahubStatusLabel() {
    if (!kvxConfig.pahubEnabled) {
        if (g_connected) return "PaHub: Hub OK (off)";
        return "PaHub: Disabled";
    }
    if (grovePortABusy() && !g_connected && unitScrollGroveBusy()) return "PaHub: Grove busy";
    if (g_connected) return "PaHub: Connected";
    return "PaHub: Not found";
}

bool grovePortABusy() { return unitScrollGroveBusy() || pahubIsConnected(); }

int8_t pahubChannelFor(PahubDevice dev) {
    if (!kvxConfig.pahubEnabled || dev == PahubDevNone) return -1;
    for (uint8_t i = 0; i < PAHUB_CH_COUNT; i++) {
        if (kvxConfig.pahubChannels[i] == (uint8_t)dev) return (int8_t)i;
    }
    return -1;
}

int8_t pahubChannelForRfidModule() {
    if (!kvxConfig.pahubEnabled) return -1;
    switch (kvxConfigPins.rfidModule) {
        case M5_RFID2_MODULE: return pahubChannelFor(PahubDevRFID2);
        case PN532_I2C_MODULE: return pahubChannelFor(PahubDevNFC);
        default: break;
    }
    return -1;
}

bool pahubSelect(int8_t ch) {
    if (!kvxConfig.pahubEnabled) return true;
    takeMutex();
    bool ok = selectLocked(ch);
    giveMutex();
    return ok;
}

bool pahubDeselect() {
    if (!kvxConfig.pahubEnabled) return true;
    takeMutex();
    bool ok = writeControl(0x00);
    giveMutex();
    return ok;
}

bool pahubSelectRf433r() {
    int8_t ch = pahubChannelFor(PahubDevRF433R);
    if (ch < 0) return true;
    return pahubSelect(ch);
}

bool pahubDeselectRf433r() {
    if (pahubChannelFor(PahubDevRF433R) < 0) return true;
    return pahubDeselect();
}

bool pahubScanChannel(uint8_t ch, PahubScanResult &out) {
    out = PahubScanResult{};
    if (ch >= PAHUB_CH_COUNT) return false;
    if (!kvxConfig.pahubEnabled) return false;

    takeMutex();
    if (!probeAndMaybeSaveAddr() || !selectLocked((int8_t)ch)) {
        giveMutex();
        return false;
    }

    TwoWire *w = acquireGroveI2C();
    if (w == nullptr) {
        giveMutex();
        return false;
    }
    for (uint8_t addr = 0x01; addr <= 0x7F && out.count < 16; addr++) {
        if (addr == kvxConfig.pahubAddr) continue;
        w->beginTransmission(addr);
        if (w->endTransmission() == 0) {
            out.addrs[out.count++] = addr;
            if (out.fingerprint == PahubDevNone) out.fingerprint = pahubFingerprintAddr(addr);
        }
    }
    writeControl(0x00);
    giveMutex();

    String hint;
    for (uint8_t i = 0; i < out.count; i++) {
        if (i) hint += " ";
        char buf[8];
        snprintf(buf, sizeof(buf), "0x%02X", out.addrs[i]);
        hint += buf;
        PahubDevice fp = pahubFingerprintAddr(out.addrs[i]);
        if (fp != PahubDevNone) {
            hint += " ";
            hint += pahubDeviceName(fp);
        }
    }
    g_hints[ch] = hint;
    return true;
}

bool pahubScanAll(PahubScanResult out[PAHUB_CH_COUNT]) {
    bool any = false;
    for (uint8_t ch = 0; ch < PAHUB_CH_COUNT; ch++) {
        if (pahubScanChannel(ch, out[ch])) any = true;
    }
    return any;
}

bool pahubGrovePollActive() { return grovePollDepth > 0; }

void pahubBeginGrovePoll() {
    if (!kvxConfig.pahubEnabled) return;
    if (!pahubDeviceOnMux(PahubDevScroll) && !pahubDeviceOnMux(PahubDevJoystick2)) return;
    if (grovePollDepth++ == 0) {
        if (!takeMutexMs(8)) {
            grovePollDepth = 0;
            return;
        }
        grovePollMutexHeld = true;
        writeControl(0x00);
    }
}

void pahubEndGrovePoll() {
    if (grovePollDepth <= 0) return;
    if (--grovePollDepth == 0 && grovePollMutexHeld) {
        writeControl(0x00);
        giveMutex();
        grovePollMutexHeld = false;
    }
}

int8_t pahubDiscoverDevice(PahubDevice dev) {
    if (dev == PahubDevNone) return -1;
    if (kvxConfig.pahubEnabled) {
        int8_t existing = pahubChannelFor(dev);
        if (existing >= 0) return existing;
    }

    if (!pahubReconnect()) return -1;

    bool wasOff = !kvxConfig.pahubEnabled;
    if (wasOff) kvxConfig.pahubEnabled = true;

    int8_t found = -1;
    for (uint8_t ch = 0; ch < PAHUB_CH_COUNT; ch++) {
        PahubScanResult out;
        if (!pahubScanChannel(ch, out)) continue;
        bool match = (out.fingerprint == dev);
        if (!match) {
            for (uint8_t i = 0; i < out.count; i++) {
                if (pahubFingerprintAddr(out.addrs[i]) == dev) {
                    match = true;
                    break;
                }
            }
        }
        if (match && kvxConfig.setPahubChannel(ch, dev)) {
            found = (int8_t)ch;
            break;
        }
    }

    if (wasOff && found < 0) kvxConfig.pahubEnabled = false;
    return found;
}

PahubChannelGuard::PahubChannelGuard(int8_t channel) {
    if (!kvxConfig.pahubEnabled || channel < 0) return;
    takeMutex();
    _held = true;
    _selected = selectLocked(channel);
}

PahubChannelGuard PahubChannelGuard::forRfid() { return PahubChannelGuard(pahubChannelForRfidModule()); }

PahubChannelGuard PahubChannelGuard::forDevice(PahubDevice dev) {
    return PahubChannelGuard(pahubChannelFor(dev));
}

PahubChannelGuard::~PahubChannelGuard() {
    if (_selected) writeControl(0x00);
    if (_held) giveMutex();
}

PahubChannelGuard::PahubChannelGuard(PahubChannelGuard &&o) noexcept
    : _held(o._held), _selected(o._selected) {
    o._held = false;
    o._selected = false;
}

PahubChannelGuard &PahubChannelGuard::operator=(PahubChannelGuard &&o) noexcept {
    if (this != &o) {
        if (_selected) writeControl(0x00);
        if (_held) giveMutex();
        _held = o._held;
        _selected = o._selected;
        o._held = false;
        o._selected = false;
    }
    return *this;
}

PahubTryGuard::PahubTryGuard(PahubDevice dev) {
    if (!kvxConfig.pahubEnabled) {
        _ok = true;
        return;
    }
    int8_t ch = pahubChannelFor(dev);
    if (ch < 0) {
        _ok = true; // device wired directly on PORT.A, not behind the mux
        return;
    }
    if (pahubGrovePollActive()) {
        _selected = selectLocked(ch);
        _ok = _selected;
        return;
    }
    if (!tryMutex()) {
        _ok = false;
        return;
    }
    _held = true;
    _selected = selectLocked(ch);
    _ok = _selected;
}

PahubTryGuard::~PahubTryGuard() {
    if (_selected) writeControl(0x00);
    if (_held) giveMutex();
}
