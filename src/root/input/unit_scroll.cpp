#include "root/input/unit_scroll.h"
#include "root/config/config.h"
#include "root/config/configPins.h"
#include "root/hal/bus_HAL.h"
#include "root/hal/pahub.h"
#include <globals.h>

#if defined(UNIT_SCROLL)
#include <M5UnitScroll.h>
#include <Wire.h>

#ifndef UNIT_SCROLL_ADDR
#define UNIT_SCROLL_ADDR 0x40
#endif

#ifndef UNIT_SCROLL_LED_IDLE
#define UNIT_SCROLL_LED_IDLE 0x001400
#endif
#ifndef UNIT_SCROLL_LED_PRESS
#define UNIT_SCROLL_LED_PRESS 0x404040
#endif

// Detents required before one menu step (1 = one physical click per menu item)
#ifndef UNIT_SCROLL_DETENTS_PER_STEP
#define UNIT_SCROLL_DETENTS_PER_STEP 1
#endif

// Hold longer than this = back; shorter tap = select
#ifndef UNIT_SCROLL_HOLD_MS
#define UNIT_SCROLL_HOLD_MS 450
#endif

static M5UnitScroll unitScroll;
static bool present = false;
static bool groveBusy = false;
static bool busHeld = false;
static int32_t detentAccumulator = 0;
static bool pendingSel = false;
static bool pendingEsc = false;
static bool btnDown = false;
static bool longPressFired = false;
static unsigned long btnDownMs = 0;
static bool lastBtn = false;
static uint8_t scrollIdlePolls = 0;

static uint8_t scrollSda() {
    if (kvxConfigPins.i2c_bus.sda >= 0) return (uint8_t)kvxConfigPins.i2c_bus.sda;
#ifdef GROVE_SDA
    return (uint8_t)GROVE_SDA;
#else
    return 2;
#endif
}

static uint8_t scrollScl() {
    if (kvxConfigPins.i2c_bus.scl >= 0) return (uint8_t)kvxConfigPins.i2c_bus.scl;
#ifdef GROVE_SCL
    return (uint8_t)GROVE_SCL;
#else
    return 1;
#endif
}

static void setIdleLed() {
    if (!present) return;
    unitScroll.setLEDColor(UNIT_SCROLL_LED_IDLE);
}

static void releaseScrollBus() {
    if (busHeld) {
        releaseI2CBusHold();
        busHeld = false;
    }
}

static bool unitScrollProbe(bool quiet, bool allowDisabled) {
    present = false;
    groveBusy = false;
    releaseScrollBus();
    detentAccumulator = 0;
    pendingSel = false;
    pendingEsc = false;
    btnDown = false;
    longPressFired = false;
    lastBtn = false;

    if (!allowDisabled && !kvxConfig.unitScrollEnabled) {
        if (!quiet) Serial.println("[UnitScroll] disabled in config");
        return false;
    }

    PahubChannelGuard mux(PahubChannelGuard::forDevice(PahubDevScroll));

    uint8_t sda = scrollSda();
    uint8_t scl = scrollScl();
    if (!quiet) Serial.printf("[UnitScroll] probe addr=0x%02X SDA=%u SCL=%u\n", UNIT_SCROLL_ADDR, sda, scl);

    TwoWire *bus = acquireI2CBus((int8_t)sda, (int8_t)scl);
    if (bus == nullptr) {
        if (!quiet) Serial.println("[UnitScroll] I2C bus unavailable");
        return false;
    }

    if (!unitScroll.begin(bus, UNIT_SCROLL_ADDR, sda, scl, 100000U)) {
        if (!quiet) Serial.println("[UnitScroll] begin failed");
        return false;
    }
    if (!unitScroll.getDevStatus()) {
        if (!quiet) Serial.println("[UnitScroll] device not present");
        return false;
    }

    present = true;
    groveBusy = true;
    holdI2CBus((int8_t)sda, (int8_t)scl);
    busHeld = true;
    unitScroll.resetEncoder();
    setIdleLed();
    if (!quiet) Serial.println("[UnitScroll] connected");
    return true;
}

bool unitScrollBegin(bool quiet) { return unitScrollProbe(quiet, false); }

bool unitScrollReconnect() {
    bool wasPresent = present;
    if (wasPresent) {
        PahubChannelGuard mux(PahubChannelGuard::forDevice(PahubDevScroll));
        if (!pahubDeviceOnMux(PahubDevScroll) || mux.active()) unitScroll.setLEDColor(0);
    }

    if (unitScrollProbe(false, true)) {
        if (!kvxConfig.unitScrollEnabled) kvxConfig.setUnitScrollEnabled(true);
        return true;
    }

    if (pahubDiscoverDevice(PahubDevScroll) >= 0 && unitScrollProbe(false, true)) {
        if (!kvxConfig.unitScrollEnabled) kvxConfig.setUnitScrollEnabled(true);
        return true;
    }

    pahubDeselect();
    if (unitScrollProbe(false, true)) {
        if (!kvxConfig.unitScrollEnabled) kvxConfig.setUnitScrollEnabled(true);
        return true;
    }

    present = false;
    groveBusy = false;
    releaseScrollBus();
    return false;
}

bool unitScrollIsPresent() { return present; }
bool unitScrollGroveBusy() { return groveBusy; }

void unitScrollPoll() {
    if (!present) return;

    if (pahubDeviceOnMux(PahubDevScroll)) {
        PahubTryGuard mux(PahubDevScroll);
        if (!mux.ok()) return;
    }

    if (!unitScroll.getDevStatus()) {
        present = false;
        groveBusy = false;
        releaseScrollBus();
        return;
    }

    int16_t inc = unitScroll.getIncEncoderValue();
    if (inc > 8 || inc < -8) {
        unitScroll.resetEncoder();
        inc = 0;
    }
    if (kvxConfig.unitScrollInvert) inc = (int16_t)(-inc);
    if (inc != 0) {
        scrollIdlePolls = 0;
        detentAccumulator += inc;
        const int step = UNIT_SCROLL_DETENTS_PER_STEP;
        while (detentAccumulator >= step) {
            RotaryNetSteps--;
            detentAccumulator -= step;
        }
        while (detentAccumulator <= -step) {
            RotaryNetSteps++;
            detentAccumulator += step;
        }
    } else if (scrollIdlePolls < 255) {
        scrollIdlePolls++;
        if (scrollIdlePolls > 40) detentAccumulator = 0;
    }

    bool btn = unitScroll.getButtonStatus();
    if (btn) {
        if (!btnDown) {
            btnDown = true;
            btnDownMs = millis();
            longPressFired = false;
        } else if (!longPressFired && (millis() - btnDownMs >= UNIT_SCROLL_HOLD_MS)) {
            pendingEsc = true;
            longPressFired = true;
        }
        unitScroll.setLEDColor(UNIT_SCROLL_LED_PRESS);
    } else {
        if (btnDown) {
            btnDown = false;
            if (!longPressFired) pendingSel = true;
        }
        if (lastBtn) setIdleLed();
    }
    lastBtn = btn;
}

void unitScrollApplyInput() {
    if (!present) return;

    if (pendingEsc) {
        EscPress = true;
        AnyKeyPress = true;
        pendingEsc = false;
    }

    if (pendingSel) {
        SelPress = true;
        AnyKeyPress = true;
        pendingSel = false;
    }
}

String unitScrollStatusLabel() {
    if (!kvxConfig.unitScrollEnabled) return "Unit Scroll: Disabled";
    if (present) {
        int8_t ch = pahubChannelFor(PahubDevScroll);
        if (pahubDeviceOnMux(PahubDevScroll)) {
            return String("Unit Scroll: Connected (ch") + ch + ")";
        }
        return "Unit Scroll: Connected (direct)";
    }
    return "Unit Scroll: Not found";
}

#else // !UNIT_SCROLL

bool unitScrollIsPresent() { return false; }
bool unitScrollGroveBusy() { return false; }
bool unitScrollBegin(bool) { return false; }
bool unitScrollReconnect() { return false; }
void unitScrollPoll() {}
void unitScrollApplyInput() {}
String unitScrollStatusLabel() { return "Unit Scroll: N/A"; }

#endif
