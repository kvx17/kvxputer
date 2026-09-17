#include "kremote_use.h"
#include "kremote_store.h"
#include "kremote_ui.h"
#include "menu/infrared/custom_ir.h"
#include "menu/infrared/ir_utils.h"
#include "root/config/config.h"
#include "root/config/configPins.h"
#include "root/ui/display.h"
#include <globals.h>

static int krPickProfileIndex(std::vector<KremoteProfile> &list, const char *title) {
    if (list.empty()) {
        displayInfo("No remotes saved", true);
        return -1;
    }
    std::vector<Option> opts;
    for (size_t i = 0; i < list.size(); i++) {
        opts.push_back({list[i].displayName.c_str(), [=]() {}});
    }
    opts.push_back({"Cancel", []() {}});
    int sel = loopOptions(opts, MENU_TYPE_SUBMENU, title);
    if (sel < 0 || sel >= (int)list.size()) return -1;
    return sel;
}

// After first edge consumed: true = hold, false = short.
// Pulse-style flags (Up/Down/Prev/Next) use repeat; sticky Esc/Sel stay high while held.
static bool krWaitHoldPulse(volatile bool &flag) {
    unsigned long t0 = millis();
    unsigned long lastPulse = t0;
    while (millis() - t0 < KREMOTE_HOLD_MS) {
        delay(12);
        if (flag) {
            flag = false;
            lastPulse = millis();
        }
        if (millis() - t0 > 60 && millis() - lastPulse > 90) return false;
    }
    unsigned long quietStart = millis();
    while (millis() - quietStart < 80) {
        if (flag) {
            flag = false;
            quietStart = millis();
        }
        delay(10);
    }
    return true;
}

static bool krWaitHoldSticky(volatile bool &flag) {
    unsigned long t0 = millis();
    while (millis() - t0 < KREMOTE_HOLD_MS) {
        delay(12);
        if (!flag) return false;
    }
    while (flag) {
        check(flag);
        delay(10);
    }
    return true;
}

static void krSendSlot(IRCode *slots[KREMOTE_SLOT_COUNT], int slot, int &flashId, unsigned long &flashUntil,
                       const char *&footerMsg) {
    flashId = kremoteFlashForSlot(slot);
    flashUntil = millis() + KREMOTE_FLASH_MS;
    if (slot < 0 || slot >= KREMOTE_SLOT_COUNT || slots[slot] == nullptr) {
        footerMsg = "not learned";
        return;
    }
    footerMsg = kremoteSlotName(slot);
    sendIRCommand(slots[slot], true);
}

static void krUseLoop(IRCode *slots[KREMOTE_SLOT_COUNT], const char *title) {
    const bool swapped = kvxConfig.kremoteButtonsSwapped;
    int flashId = KREMOTE_FLASH_NONE;
    unsigned long flashUntil = 0;
    const char *footerMsg = "hold Back=exit  x2=Power";
    unsigned long pendingBackAt = 0;
    bool pendingBack = false;

    auto redraw = [&]() {
        kremoteDrawHeader(title);
        kremoteDrawPad(kvxConfig.kremotePortrait, swapped, flashId);
        kremoteDrawFooter(footerMsg);
    };

    tft.fillScreen(KVX_DEFAULT_BGCOLOR);
    redraw();

    setup_ir_pin(kvxConfigPins.irTx, OUTPUT);

    while (true) {
        if (flashId != KREMOTE_FLASH_NONE && millis() > flashUntil) {
            flashId = KREMOTE_FLASH_NONE;
            footerMsg = "hold Back=exit  x2=Power";
            redraw();
        }

        // Resolve deferred Back (waiting for possible double-tap)
        if (pendingBack && millis() - pendingBackAt >= KREMOTE_DOUBLE_TAP_MS) {
            pendingBack = false;
            krSendSlot(slots, KREMOTE_BACK, flashId, flashUntil, footerMsg);
            redraw();
        }

        if (check(UpPress)) {
            bool hold = krWaitHoldPulse(UpPress);
            int slot = kremoteResolveSlot(KREMOTE_PHYS_UP, hold, swapped);
            krSendSlot(slots, slot, flashId, flashUntil, footerMsg);
            redraw();
            continue;
        }
        if (check(DownPress)) {
            bool hold = krWaitHoldPulse(DownPress);
            int slot = kremoteResolveSlot(KREMOTE_PHYS_DOWN, hold, swapped);
            krSendSlot(slots, slot, flashId, flashUntil, footerMsg);
            redraw();
            continue;
        }
        if (check(PrevPress)) {
            bool hold = krWaitHoldPulse(PrevPress);
            int slot = kremoteResolveSlot(KREMOTE_PHYS_LEFT, hold, swapped);
            krSendSlot(slots, slot, flashId, flashUntil, footerMsg);
            redraw();
            continue;
        }
        if (check(NextPress)) {
            bool hold = krWaitHoldPulse(NextPress);
            int slot = kremoteResolveSlot(KREMOTE_PHYS_RIGHT, hold, swapped);
            krSendSlot(slots, slot, flashId, flashUntil, footerMsg);
            redraw();
            continue;
        }
        if (check(SelPress)) {
            bool hold = krWaitHoldSticky(SelPress);
            int slot = kremoteResolveSlot(KREMOTE_PHYS_OK, hold, swapped);
            krSendSlot(slots, slot, flashId, flashUntil, footerMsg);
            redraw();
            continue;
        }
        if (EscPress) {
            // Peek without consuming until we know short vs hold
            bool hold = krWaitHoldSticky(EscPress);
            if (hold) {
                pendingBack = false;
                break; // exit Use Remote
            }
            // Short Back — double-tap → Power
            if (pendingBack && millis() - pendingBackAt < KREMOTE_DOUBLE_TAP_MS) {
                pendingBack = false;
                krSendSlot(slots, KREMOTE_POWER, flashId, flashUntil, footerMsg);
                redraw();
            } else {
                pendingBack = true;
                pendingBackAt = millis();
            }
            continue;
        }

        delay(8);
    }

    digitalWrite(kvxConfigPins.irTx, LED_OFF);
}

void kremoteUseFlow() {
    auto list = kremoteListProfiles();
    int idx = krPickProfileIndex(list, "Use Remote");
    if (idx < 0) return;

    IRCode *slots[KREMOTE_SLOT_COUNT] = {};
    if (!kremoteLoadSlots(list[idx].fs, list[idx].path, slots)) {
        displayError("Failed to load", true);
        return;
    }

#if defined(HAS_SCREEN)
    if (kvxConfig.kremotePortrait) {
        KremoteVertDisplayScope scope;
        krUseLoop(slots, list[idx].displayName.c_str());
    } else {
        krUseLoop(slots, list[idx].displayName.c_str());
    }
#else
    krUseLoop(slots, list[idx].displayName.c_str());
#endif

    kremoteFreeSlots(slots);
}

void kremoteDeleteFlow() {
    auto list = kremoteListProfiles();
    int idx = krPickProfileIndex(list, "Delete Remote");
    if (idx < 0) return;

    bool confirmed = false;
    std::vector<Option> confirm = {
        {"Delete " + list[idx].displayName, [&]() { confirmed = true; }},
        {"Cancel", []() {}},
    };
    loopOptions(confirm, MENU_TYPE_SUBMENU, "Confirm");
    if (!confirmed) return;

    if (kremoteDeleteProfile(list[idx])) displaySuccess("Deleted", true);
    else displayError("Delete failed", true);
}
