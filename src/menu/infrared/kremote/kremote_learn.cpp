#include "kremote_learn.h"
#include "kremote_store.h"
#include "kremote_use.h"
#include "menu/infrared/ir_utils.h"
#include "root/config/configPins.h"
#include "root/input/mykeyboard.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#include "root/ui/scrollableTextArea.h"
#include "root/ui/settings.h"
#include <IRrecv.h>
#include <IRutils.h>
#include <globals.h>

#define KREMOTE_IR_FREQ 38000

static String krUint32ToString(uint32_t value) {
    char buffer[12] = {0};
    snprintf(
        buffer,
        sizeof(buffer),
        "%02lX %02lX %02lX %02lX",
        value & 0xFF,
        (value >> 8) & 0xFF,
        (value >> 16) & 0xFF,
        (value >> 24) & 0xFF
    );
    return String(buffer);
}

static String krUint32ToStringInverted(uint32_t value) {
    char buffer[12] = {0};
    snprintf(
        buffer,
        sizeof(buffer),
        "%02lX %02lX %02lX %02lX",
        (value >> 24) & 0xFF,
        (value >> 16) & 0xFF,
        (value >> 8) & 0xFF,
        value & 0xFF
    );
    return String(buffer);
}

static uint32_t krNecCommandWithInverse(uint32_t command) {
    return (command & 0xFF) | (((~command) & 0xFF) << 8);
}

static String krProtocolName(const decode_results &r) {
    switch (r.decode_type) {
        case decode_type_t::RC5: return (r.command > 0x3F) ? "RC5X" : "RC5";
        case decode_type_t::RC6: return "RC6";
        case decode_type_t::SAMSUNG: return "Samsung32";
        case decode_type_t::SONY:
            if (r.address > 0xFF) return "SIRC20";
            if (r.address > 0x1F) return "SIRC15";
            return "SIRC";
        case decode_type_t::NEC: return (r.address > 0xFF) ? "NECext" : "NEC";
        case decode_type_t::UNKNOWN: return "";
        default: return typeToString(r.decode_type, r.repeat);
    }
}

static String krParseRaw(decode_results &results) {
    uint16_t *rawcode = resultToRawArray(&results);
    uint16_t len = getCorrectedRawLength(&results);
    String signal;
    for (uint16_t i = 0; i < len; i++) {
        signal += String(rawcode[i]);
        signal += " ";
    }
    delete[] rawcode;
    signal.trim();
    return signal;
}

static IRCode *krResultsToCode(decode_results &results) {
    IRCode *code = new IRCode();
    bool useRaw = (results.decode_type == decode_type_t::UNKNOWN) || hasACState(results.decode_type);
    String proto = krProtocolName(results);
    if (useRaw || proto.length() == 0) {
        code->type = "raw";
        code->frequency = KREMOTE_IR_FREQ;
        code->data = krParseRaw(results);
        code->protocol = "";
    } else {
        code->type = "parsed";
        code->protocol = proto;
        code->address = krUint32ToString(results.address);
        code->command = (results.decode_type == decode_type_t::NEC)
                            ? krUint32ToString(krNecCommandWithInverse(results.command))
                            : krUint32ToString(results.command);
        code->bits = results.bits;
        if (hasACState(results.decode_type)) {
            String state;
            uint16_t state_len = results.bits / 8;
            for (uint16_t i = 0; i < state_len; i++) {
                if (results.state[i] < 0x10) state += "0";
                state += String(results.state[i], HEX);
                state += " ";
            }
            state.toUpperCase();
            state.trim();
            code->data = state;
        } else if (results.bits > 32) {
            code->data = krUint32ToString(results.value) + " " + krUint32ToString(results.value >> 32);
        } else {
            code->data = krUint32ToStringInverted(results.value);
        }
    }
    return code;
}

static void krLearnPaint(ScrollableTextArea &area, std::initializer_list<String> lines) {
    area.clear();
    for (const String &line : lines) area.addLine(line);
    area.draw(true);
}

static void krLearnBanner(ScrollableTextArea &area, const char *slotName, int index) {
    {
        TftFrame frame;
        tft.fillScreen(kvxConfig.bgColor);
        drawKvxTopBar("Learn Rem");
    }
    krLearnPaint(
        area,
        {
            "Button " + String(index + 1) + "/" + String(KREMOTE_SLOT_COUNT),
            String("Capture: ") + slotName,
            "",
            "Point remote <30cm",
            "OK=accept  Rt=skip",
            "Lt=retry   Esc=save",
        }
    );
}

enum KrCaptureAction { KR_ACCEPT, KR_SKIP, KR_RETRY, KR_ABORT_SAVE };

static KrCaptureAction krCaptureSlot(IRrecv &irrecv, const char *slotName, int index, IRCode **outCode) {
    *outCode = nullptr;
    decode_results results;
    irrecv.enableIRIn();
    irrecv.resume();

    const int bodyY = KVX_TOPBAR_H + 4;
    const int bodyH = tftHeight - bodyY - 4;
    ScrollableTextArea area(
        uiBodyFont(), BORDER_PAD_X, bodyY, tftWidth - 2 * BORDER_PAD_X, bodyH, false, false
    );

    while (true) {
        krLearnBanner(area, slotName, index);
        area.addLine("");
        area.addLine("Waiting for signal...");
        area.draw(true);

        bool got = false;
        while (!got) {
            area.update(false);
            if (check(EscPress)) return KR_ABORT_SAVE;
            if (check(NextPress)) return KR_SKIP; // skip before capture
            if (irrecv.decode(&results)) {
                got = true;
                break;
            }
            delay(10);
        }

        IRCode *pending = krResultsToCode(results);
        {
            TftFrame frame;
            tft.fillScreen(kvxConfig.bgColor);
            drawKvxTopBar("Learn Rem");
        }
        area.clear();
        area.addLine(String("Got: ") + slotName);
        if (pending->type == "raw") {
            area.addLine("Type: raw");
            area.addLine(pending->data.substring(0, 40) + "...");
        } else {
            area.addLine("Proto: " + pending->protocol);
            area.addLine("Addr: " + pending->address);
            area.addLine("Cmd: " + pending->command);
        }
        area.addLine("");
        area.addLine("OK=accept  Rt=skip");
        area.addLine("Lt=retry   Esc=save");
        area.draw(true);

        while (true) {
            area.update(false);
            if (check(SelPress)) {
                *outCode = pending;
                irrecv.resume();
                return KR_ACCEPT;
            }
            if (check(NextPress)) {
                delete pending;
                irrecv.resume();
                return KR_SKIP;
            }
            if (check(PrevPress)) {
                delete pending;
                irrecv.resume();
                return KR_RETRY;
            }
            if (check(EscPress)) {
                delete pending;
                return KR_ABORT_SAVE;
            }
            delay(10);
        }
    }
}

static int krPickProfile(std::vector<KremoteProfile> &list, const char *title) {
    if (list.empty()) return -1;
    std::vector<Option> opts;
    for (size_t i = 0; i < list.size(); i++) {
        opts.push_back({list[i].displayName.c_str(), [=]() {}});
    }
    opts.push_back({"Cancel", []() {}});
    int sel = loopOptions(opts, MENU_TYPE_SUBMENU, title);
    if (sel < 0 || sel >= (int)list.size()) return -1;
    return sel;
}

void kremoteLearnFlow() {
    struct KrIrGuard {
        KrIrGuard() { kremoteBeginIrHw(); }
        ~KrIrGuard() { kremoteEndIrHw(); }
    } hw;
    std::vector<Option> modeOpts = {
        {"New Remote", []() {}},
        {"Update Remote", []() {}},
        {"Cancel", []() {}},
    };
    int mode = loopOptions(modeOpts, MENU_TYPE_SUBMENU, "Learn Rem");
    if (mode < 0 || mode == 2) return;

    FS *fs = kremotePickFs();
    if (fs == nullptr) {
        displayError("No storage available", true);
        return;
    }

    IRCode *slots[KREMOTE_SLOT_COUNT] = {};
    String slug;

    if (mode == 1) {
        auto list = kremoteListProfiles();
        int idx = krPickProfile(list, "Update Remote");
        if (idx < 0) return;
        slug = list[idx].displayName;
        if (!kremoteLoadSlots(list[idx].fs, list[idx].path, slots)) {
            displayError("Failed to load profile", true);
            return;
        }
        fs = list[idx].fs;
    } else {
        String name = keyboard("living_room", KREMOTE_NAME_MAX, "Remote name:");
        if (name == "\x1B" || name.length() == 0) return;
        slug = kremoteSanitizeName(name);
        if (slug.length() == 0) {
            displayError("Invalid name", true);
            return;
        }
        if (kremoteProfileExists(slug)) {
            displayError("Name already exists", true);
            return;
        }
    }

    const std::vector<std::pair<String, int>> pins = IR_RX_PINS;
    int count = 0;
    for (auto pin : pins) {
        if (pin.second == kvxConfigPins.irRx) count++;
    }
    if (count == 0) gsetIrRxPin(true);

    setup_ir_pin(kvxConfigPins.irRx, INPUT);
    IRrecv irrecv(kvxConfigPins.irRx, SAFE_STACK_BUFFER_SIZE / 2, 50);

    bool abortSave = false;
    for (int i = 0; i < KREMOTE_SLOT_COUNT; i++) {
        while (true) {
            IRCode *captured = nullptr;
            KrCaptureAction act = krCaptureSlot(irrecv, kremoteSlotName(i), i, &captured);
            if (act == KR_ACCEPT) {
                delete slots[i];
                slots[i] = captured;
                slots[i]->name = kremoteSlotName(i);
                break;
            }
            if (act == KR_SKIP) {
                // Update: keep existing; New: leave null
                break;
            }
            if (act == KR_RETRY) continue;
            if (act == KR_ABORT_SAVE) {
                abortSave = true;
                break;
            }
        }
        if (abortSave) break;
    }

    irrecv.disableIRIn();

    int have = 0;
    for (int i = 0; i < KREMOTE_SLOT_COUNT; i++) {
        if (slots[i]) have++;
    }
    if (have == 0) {
        displayWarning("Nothing to save", true);
        kremoteFreeSlots(slots);
        return;
    }

    String path = kremotePathFor(slug);
    if (kremoteSaveSlots(fs, path, slug, slots)) {
        displaySuccess("Saved " + String(have) + " buttons", true);
    } else {
        displayError("Save failed", true);
    }
    kremoteFreeSlots(slots);
}
