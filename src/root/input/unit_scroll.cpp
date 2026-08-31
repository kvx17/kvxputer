#include "root/input/unit_scroll.h"
#include "root/config/config.h"
#include "root/config/configPins.h"
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
static int32_t detentAccumulator = 0;
static bool pendingSel = false;
static bool pendingEsc = false;
static bool btnDown = false;
static bool longPressFired = false;
static unsigned long btnDownMs = 0;
static bool lastBtn = false;

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

bool unitScrollBegin(bool quiet) {
    present = false;
    groveBusy = false;
    detentAccumulator = 0;
    pendingSel = false;
    pendingEsc = false;
    btnDown = false;
    longPressFired = false;
    lastBtn = false;

    if (!kvxConfig.unitScrollEnabled) {
        if (!quiet) Serial.println("[UnitScroll] disabled in config");
        return false;
    }

    uint8_t sda = scrollSda();
    uint8_t scl = scrollScl();
    if (!quiet) Serial.printf("[UnitScroll] probe addr=0x%02X SDA=%u SCL=%u\n", UNIT_SCROLL_ADDR, sda, scl);

    if (!unitScroll.begin(&Wire, UNIT_SCROLL_ADDR, sda, scl, 100000U)) {
        if (!quiet) Serial.println("[UnitScroll] begin failed");
        return false;
    }
    if (!unitScroll.getDevStatus()) {
        if (!quiet) Serial.println("[UnitScroll] device not present");
        return false;
    }

    present = true;
    groveBusy = true;
    unitScroll.resetEncoder();
    setIdleLed();
    if (!quiet) Serial.println("[UnitScroll] connected");
    return true;
}

bool unitScrollReconnect() {
    if (present) unitScroll.setLEDColor(0);
    bool ok = unitScrollBegin(false);
    if (!ok) {
        present = false;
        groveBusy = false;
    }
    return ok;
}

bool unitScrollIsPresent() { return present; }
bool unitScrollGroveBusy() { return groveBusy; }

void unitScrollPoll() {
    if (!present || !kvxConfig.unitScrollEnabled) return;

    if (!unitScroll.getDevStatus()) {
        present = false;
        groveBusy = false;
        return;
    }

    int16_t inc = unitScroll.getIncEncoderValue();
    if (kvxConfig.unitScrollInvert) inc = (int16_t)(-inc);
    if (inc != 0) {
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
    if (present) return "Unit Scroll: Connected";
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
