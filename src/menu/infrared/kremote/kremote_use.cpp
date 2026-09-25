#include "kremote_use.h"
#include "kremote_store.h"
#include "kremote_ui.h"
#include "kremote_config.h"
#include "menu/infrared/custom_ir.h"
#include "menu/infrared/ir_utils.h"
#include "root/config/config.h"
#include "root/config/configPins.h"
#include "root/input/mykeyboard.h"
#include "root/storage/paths.h"
#include "root/ui/display.h"
#include <cctype>
#include <globals.h>

static int s_savedIrTx = -1;
static int s_savedIrRx = -1;
static int s_irHwDepth = 0;

static int kremoteOnboardTxPin() {
#ifdef TXLED
    return TXLED;
#else
    return kvxConfigPins.irTx;
#endif
}

static int kremoteUnitTxPin() {
#ifdef GROVE_SDA
    return GROVE_SDA;
#else
    return -1;
#endif
}

static int kremoteUnitRxPin() {
#ifdef GROVE_SCL
    return GROVE_SCL;
#else
    return -1;
#endif
}

const char *kremoteIrHwLabel() {
    switch (kvxConfig.kremoteIrHw) {
        case KREMOTE_IR_UNIT: return "Unit IR";
        case KREMOTE_IR_BOTH: return "Both";
        default: return "Cardputer IR";
    }
}

int kremoteLearnRxPin() {
    if (kvxConfig.kremoteIrHw != KREMOTE_IR_ONBOARD) {
        int p = kremoteUnitRxPin();
        if (p >= 0) return p;
    }
    return kvxConfigPins.irRx;
}

void kremoteBeginIrHw() {
    if (s_irHwDepth++ > 0) return;
    s_savedIrTx = kvxConfigPins.irTx;
    s_savedIrRx = kvxConfigPins.irRx;
    int onboard = kremoteOnboardTxPin();
    int unitTx = kremoteUnitTxPin();
    int primary = onboard;
    int extra = -1;
    switch (kvxConfig.kremoteIrHw) {
        case KREMOTE_IR_UNIT:
            if (unitTx >= 0) primary = unitTx;
            extra = -1;
            break;
        case KREMOTE_IR_BOTH:
            primary = onboard;
            extra = (unitTx >= 0 && unitTx != onboard) ? unitTx : -1;
            break;
        default:
            primary = onboard;
            extra = -1;
            break;
    }
    kvxConfigPins.irTx = primary;
    kvxConfigPins.irRx = kremoteLearnRxPin();
    setIrTxExtraPin(extra);
    setup_ir_pin(primary, OUTPUT);
    if (extra >= 0) setup_ir_pin(extra, OUTPUT);
}

void kremoteEndIrHw() {
    if (s_irHwDepth <= 0) return;
    if (--s_irHwDepth > 0) return;
    int extra = getIrTxExtraPin();
    if (extra >= 0) digitalWrite(extra, LED_OFF);
    if (kvxConfigPins.irTx >= 0) digitalWrite(kvxConfigPins.irTx, LED_OFF);
    setIrTxExtraPin(-1);
    kvxConfigPins.irTx = s_savedIrTx;
    kvxConfigPins.irRx = s_savedIrRx;
}

String kremoteBrowseStartFolder() {
    String p = kvxConfig.kremoteBrowseFolder;
    p.trim();
    if (p.length() == 0) return String(kvx::paths::IR_REMOTES);
    return p;
}

struct KremoteIrSession {
    KremoteIrSession() { kremoteBeginIrHw(); }
    ~KremoteIrSession() { kremoteEndIrHw(); }
};

// Submenu-chrome picker so F can toggle favorites without changing loopOptions.
static int krPickProfileIndex(
    std::vector<KremoteProfile> &list, const char *title, bool favoritable
) {
    if (list.empty()) {
        displayInfo("No remotes saved", true);
        return -1;
    }

    auto favs = kremoteLoadFavorites();
    auto isFav = [&](const String &slug) -> bool {
        for (const auto &f : favs) {
            if (f == slug) return true;
        }
        return false;
    };

    int index = 0;
    bool redraw = true;
    while (!forceHome) {
        if (redraw) {
            std::vector<Option> opts;
            opts.reserve(list.size() + 1);
            for (size_t i = 0; i < list.size(); i++) {
                String label = list[i].displayName;
                if (isFav(list[i].displayName)) label = String("* ") + label;
                opts.push_back({label, []() {}});
            }
            opts.push_back({"Cancel", []() {}});
            drawSubmenu(index, opts, title);
            if (favoritable) {
                tft.setTextSize(uiDenseFont());
                tft.setTextColor(kvxConfig.secColor, kvxConfig.bgColor);
                tft.drawCentreString(
                    "[F] favorite", tftWidth / 2, tftHeight - uiLineH(uiDenseFont()) - 2, 1
                );
            }
            redraw = false;
        }

        if (check(EscPress)) return -1;
        if (check(PrevPress) || check(UpPress)) {
            if (index == 0) index = (int)list.size(); // Cancel
            else index--;
            redraw = true;
        }
        if (check(NextPress) || check(DownPress)) {
            if (index >= (int)list.size()) index = 0;
            else index++;
            redraw = true;
        }
#ifdef HAS_KEYBOARD
        if (favoritable) {
            char letter = checkLetterShortcutPress();
            if (letter == 'f' || letter == 'F') {
                if (index >= 0 && index < (int)list.size()) {
                    kremoteToggleFavorite(list[index].displayName);
                    favs = kremoteLoadFavorites();
                    redraw = true;
                }
            }
        }
#endif
        if (check(SelPress)) {
            if (index < 0 || index >= (int)list.size()) return -1;
            return index;
        }
        delay(10);
    }
    return -1;
}

static String kremoteNormName(const String &raw) {
    String out;
    out.reserve(raw.length());
    for (size_t i = 0; i < raw.length(); i++) {
        char c = raw[i];
        if (c == ' ' || c == '_' || c == '-') continue;
        out += (char)toupper((unsigned char)c);
    }
    return out;
}

static IRCode *kremoteFindByAliases(const std::vector<IRCode *> &codes, const char *const *aliases) {
    for (int a = 0; aliases[a] != nullptr; a++) {
        String want = kremoteNormName(String(aliases[a]));
        for (IRCode *c : codes) {
            if (c && kremoteNormName(c->name) == want) return c;
        }
    }
    return nullptr;
}

static void kremoteResolveBindings(const std::vector<IRCode *> &codes, IRCode *bound[KREMOTE_ACT_COUNT]) {
    for (int i = 0; i < KREMOTE_ACT_COUNT; i++) bound[i] = nullptr;

    static const char *const upA[] = {"UP", nullptr};
    static const char *const dnA[] = {"DOWN", nullptr};
    static const char *const ltA[] = {"LEFT", nullptr};
    static const char *const rtA[] = {"RIGHT", nullptr};
    static const char *const okA[] = {"OK", "ENTER", "SELECT", nullptr};
    static const char *const vuA[] = {"VOL+", "VOL_UP", "VOLUME_UP", "V+", "VOLUP", nullptr};
    static const char *const vdA[] = {"VOL-", "VOL_DOWN", "VOLUME_DOWN", "V-", "VOLDOWN", nullptr};
    static const char *const cuA[] = {"CHA+", "CH+", "CH_UP", "CHANNEL_UP", "CHUP", nullptr};
    static const char *const cdA[] = {"CHA-", "CH-", "CH_DOWN", "CHANNEL_DOWN", "CHDOWN", nullptr};
    static const char *const poffA[] = {"POWER_OFF", "OFF", "SHUTDOWN", "POWER OFF", nullptr};
    static const char *const ponA[] = {"POWER_ON", "ON", "POWER ON", nullptr};
    static const char *const powerA[] = {"POWER", nullptr};
    static const char *const backA[] = {"BACK", nullptr};
    static const char *const homeA[] = {"HOME", nullptr};
    static const char *const menuA[] = {"MENU", nullptr};
    static const char *const muteA[] = {"MUTE", nullptr};
    static const char *const playA[] = {"PLAY", "PAUSE", "PLAYPAUSE", "PLAY_PAUSE", nullptr};
    static const char *const ffA[] = {"FASTFORWARD", "FF", "FORWARD", "NEXT", nullptr};
    static const char *const rwA[] = {"REWIND", "RW", "PREV", "PREVIOUS", nullptr};

    bound[KREMOTE_ACT_UP] = kremoteFindByAliases(codes, upA);
    bound[KREMOTE_ACT_DOWN] = kremoteFindByAliases(codes, dnA);
    bound[KREMOTE_ACT_LEFT] = kremoteFindByAliases(codes, ltA);
    bound[KREMOTE_ACT_RIGHT] = kremoteFindByAliases(codes, rtA);
    bound[KREMOTE_ACT_OK] = kremoteFindByAliases(codes, okA);
    bound[KREMOTE_ACT_VOL_UP] = kremoteFindByAliases(codes, vuA);
    bound[KREMOTE_ACT_VOL_DOWN] = kremoteFindByAliases(codes, vdA);
    bound[KREMOTE_ACT_CH_UP] = kremoteFindByAliases(codes, cuA);
    bound[KREMOTE_ACT_CH_DOWN] = kremoteFindByAliases(codes, cdA);
    bound[KREMOTE_ACT_BACK] = kremoteFindByAliases(codes, backA);
    bound[KREMOTE_ACT_HOME] = kremoteFindByAliases(codes, homeA);
    bound[KREMOTE_ACT_MENU] = kremoteFindByAliases(codes, menuA);
    bound[KREMOTE_ACT_MUTE] = kremoteFindByAliases(codes, muteA);
    bound[KREMOTE_ACT_PLAY] = kremoteFindByAliases(codes, playA);
    bound[KREMOTE_ACT_FORWARD] = kremoteFindByAliases(codes, ffA);
    bound[KREMOTE_ACT_REWIND] = kremoteFindByAliases(codes, rwA);

    bound[KREMOTE_ACT_POWER_OFF] = kremoteFindByAliases(codes, poffA);
    bound[KREMOTE_ACT_POWER_ON] = kremoteFindByAliases(codes, ponA);
    IRCode *power = kremoteFindByAliases(codes, powerA);
    if (power) {
        if (!bound[KREMOTE_ACT_POWER_OFF]) bound[KREMOTE_ACT_POWER_OFF] = power;
        if (!bound[KREMOTE_ACT_POWER_ON]) bound[KREMOTE_ACT_POWER_ON] = power;
    }

    static const char *const d1[] = {"1", "NUM_1", "DIGIT_1", nullptr};
    static const char *const d2[] = {"2", "NUM_2", "DIGIT_2", nullptr};
    static const char *const d3[] = {"3", "NUM_3", "DIGIT_3", nullptr};
    static const char *const d4[] = {"4", "NUM_4", "DIGIT_4", nullptr};
    static const char *const d5[] = {"5", "NUM_5", "DIGIT_5", nullptr};
    static const char *const d6[] = {"6", "NUM_6", "DIGIT_6", nullptr};
    static const char *const d7[] = {"7", "NUM_7", "DIGIT_7", nullptr};
    static const char *const d8[] = {"8", "NUM_8", "DIGIT_8", nullptr};
    static const char *const d9[] = {"9", "NUM_9", "DIGIT_9", nullptr};
    static const char *const d0[] = {"0", "NUM_0", "DIGIT_0", nullptr};
    bound[KREMOTE_ACT_DIGIT_1] = kremoteFindByAliases(codes, d1);
    bound[KREMOTE_ACT_DIGIT_2] = kremoteFindByAliases(codes, d2);
    bound[KREMOTE_ACT_DIGIT_3] = kremoteFindByAliases(codes, d3);
    bound[KREMOTE_ACT_DIGIT_4] = kremoteFindByAliases(codes, d4);
    bound[KREMOTE_ACT_DIGIT_5] = kremoteFindByAliases(codes, d5);
    bound[KREMOTE_ACT_DIGIT_6] = kremoteFindByAliases(codes, d6);
    bound[KREMOTE_ACT_DIGIT_7] = kremoteFindByAliases(codes, d7);
    bound[KREMOTE_ACT_DIGIT_8] = kremoteFindByAliases(codes, d8);
    bound[KREMOTE_ACT_DIGIT_9] = kremoteFindByAliases(codes, d9);
    bound[KREMOTE_ACT_DIGIT_0] = kremoteFindByAliases(codes, d0);
}

static int kremoteActionFromChar(char c) {
    switch (c) {
        case '1': return KREMOTE_ACT_DIGIT_1;
        case '2': return KREMOTE_ACT_DIGIT_2;
        case '3': return KREMOTE_ACT_DIGIT_3;
        case '4': return KREMOTE_ACT_DIGIT_4;
        case '5': return KREMOTE_ACT_DIGIT_5;
        case '6': return KREMOTE_ACT_DIGIT_6;
        case '7': return KREMOTE_ACT_DIGIT_7;
        case '8': return KREMOTE_ACT_DIGIT_8;
        case '9': return KREMOTE_ACT_DIGIT_9;
        case '0': return KREMOTE_ACT_DIGIT_0;
        case '-': return KREMOTE_ACT_VOL_DOWN;
        case '=':
        case '+': return KREMOTE_ACT_VOL_UP;
        case '[':
        case '{': return KREMOTE_ACT_CH_DOWN;
        case ']':
        case '}': return KREMOTE_ACT_CH_UP;
        case 'm':
        case 'M': return KREMOTE_ACT_MENU;
        case 'b':
        case 'B': return KREMOTE_ACT_BACK;
        case 'h':
        case 'H': return KREMOTE_ACT_HOME;
        case 'u':
        case 'U': return KREMOTE_ACT_MUTE;
        case 'o':
        case 'O': return KREMOTE_ACT_POWER_ON;
        case ' ':
        case 'p':
        case 'P': return KREMOTE_ACT_PLAY;
        case 'f':
        case 'F': return KREMOTE_ACT_FORWARD;
        case 'r':
        case 'R': return KREMOTE_ACT_REWIND;
        default: return KREMOTE_ACT_NONE;
    }
}

static int kremoteDigitFromAction(int action) {
    if (action == KREMOTE_ACT_DIGIT_0) return 0;
    if (action >= KREMOTE_ACT_DIGIT_1 && action <= KREMOTE_ACT_DIGIT_9) {
        return action - KREMOTE_ACT_DIGIT_1 + 1;
    }
    return -1;
}

void kremoteVirtualRemote(FS *fs, const String &filepath, const String &displayName) {
    KremoteIrSession hw;

    std::vector<IRCode *> codes;
    if (!kremoteLoadAllCodes(fs, filepath, codes)) {
        displayError("Failed to load", true);
        return;
    }

    IRCode *bound[KREMOTE_ACT_COUNT] = {};
    kremoteResolveBindings(codes, bound);

    bool present[KREMOTE_ACT_COUNT];
    for (int i = 0; i < KREMOTE_ACT_COUNT; i++) present[i] = (bound[i] != nullptr);

    int flashId = KREMOTE_FLASH_NONE;
    int flashDigit = -1;
    unsigned long flashUntil = 0;
    String footerOwned = "Del=exit";
    const char *footerMsg = footerOwned.c_str();

    int holdAction = KREMOTE_ACT_NONE;
    unsigned long nextRepeatAt = 0;

    auto redraw = [&]() {
        kremoteDrawVirtualRemote(displayName.c_str(), present, flashId, flashDigit);
        kremoteDrawFooter(footerMsg);
    };

    auto fireAction = [&](int action) {
        flashId = kremoteFlashForAction(action);
        flashDigit = kremoteDigitFromAction(action);
        flashUntil = millis() + KREMOTE_FLASH_MS;
        if (action < 0 || action >= KREMOTE_ACT_COUNT || bound[action] == nullptr) {
            footerOwned = "not in file";
            footerMsg = footerOwned.c_str();
            return;
        }
        footerOwned = bound[action]->name;
        footerMsg = footerOwned.c_str();
        sendIRCommand(bound[action], true);
    };

    auto beginHold = [&](int action) {
        fireAction(action);
        holdAction = action;
        nextRepeatAt = millis() + KREMOTE_REPEAT_MS;
        redraw();
    };

#if defined(HAS_SCREEN)
    tft.fillScreen(KVX_DEFAULT_BGCOLOR);
#endif
    redraw();
    setup_ir_pin(kvxConfigPins.irTx, OUTPUT);

    while (true) {
        if (forceHome) break;

        if (flashId != KREMOTE_FLASH_NONE && millis() > flashUntil) {
            flashId = KREMOTE_FLASH_NONE;
            flashDigit = -1;
            footerOwned = "Del=exit";
            footerMsg = footerOwned.c_str();
            redraw();
        }

        if (holdAction != KREMOTE_ACT_NONE && millis() >= nextRepeatAt) {
            fireAction(holdAction);
            nextRepeatAt = millis() + KREMOTE_REPEAT_MS;
            redraw();
        }

        // Del exits; Esc / ` only power-off
        if (KeyStroke.pressed && KeyStroke.del) {
            KeyStroke.Clear();
            break;
        }

        if (EscPress) {
            EscPress = false;
            if (forceHome) break;
            beginHold(KREMOTE_ACT_POWER_OFF);
            // Esc is sticky while held — keep repeating until released
            while (EscPress && !forceHome) {
                if (millis() >= nextRepeatAt) {
                    fireAction(KREMOTE_ACT_POWER_OFF);
                    nextRepeatAt = millis() + KREMOTE_REPEAT_MS;
                    redraw();
                }
                delay(12);
            }
            holdAction = KREMOTE_ACT_NONE;
            continue;
        }

        if (check(UpPress)) {
            PrevPress = false; // `;` also sets PrevPress
            if (holdAction != KREMOTE_ACT_UP) beginHold(KREMOTE_ACT_UP);
            continue;
        }
        if (check(DownPress)) {
            NextPress = false;
            if (holdAction != KREMOTE_ACT_DOWN) beginHold(KREMOTE_ACT_DOWN);
            continue;
        }
        if (check(PrevPagePress) || check(PrevPress)) {
            if (holdAction != KREMOTE_ACT_LEFT) beginHold(KREMOTE_ACT_LEFT);
            continue;
        }
        if (check(NextPagePress) || check(NextPress)) {
            if (holdAction != KREMOTE_ACT_RIGHT) beginHold(KREMOTE_ACT_RIGHT);
            continue;
        }
        if (check(SelPress)) {
            if (holdAction != KREMOTE_ACT_OK) beginHold(KREMOTE_ACT_OK);
            continue;
        }

        if (KeyStroke.pressed && !KeyStroke.word.empty()) {
            int action = KREMOTE_ACT_NONE;
            for (char c : KeyStroke.word) {
                // Arrow glyphs already handled via press flags
                if (c == ';' || c == '.' || c == ',' || c == '/' || c == '`') continue;
                action = kremoteActionFromChar(c);
                if (action != KREMOTE_ACT_NONE) break;
            }
            KeyStroke.Clear();
            if (action != KREMOTE_ACT_NONE) {
                if (holdAction != action) beginHold(action);
                continue;
            }
        }

        // Release hold when no relevant keys remain
        if (holdAction != KREMOTE_ACT_NONE) {
            bool still =
                UpPress || DownPress || PrevPress || NextPress || PrevPagePress || NextPagePress || SelPress ||
                EscPress || (KeyStroke.pressed && !KeyStroke.word.empty());
            if (!still) holdAction = KREMOTE_ACT_NONE;
        }

        delay(8);
    }

    if (kvxConfigPins.irTx >= 0) digitalWrite(kvxConfigPins.irTx, LED_OFF);
    kremoteFreeAllCodes(codes);
}

void kremoteUseFlow() {
    KremoteIrSession hw;
    auto list = kremoteListProfiles();
    int idx = krPickProfileIndex(list, "Use Remote", true);
    if (idx < 0) return;
    kremoteVirtualRemote(list[idx].fs, list[idx].path, list[idx].displayName);
}

void kremoteFavoritesFlow() {
    KremoteIrSession hw;
    while (!forceHome) {
        auto list = kremoteListFavoriteProfiles();
        if (list.empty()) {
            displayInfo("No favorites", true);
            return;
        }
        int idx = krPickProfileIndex(list, "Favorites", true);
        if (idx < 0) return;
        irFileActionMenu(list[idx].fs, list[idx].path);
    }
}

void kremoteDeleteFlow() {
    auto list = kremoteListProfiles();
    int idx = krPickProfileIndex(list, "Delete Remote", false);
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

void kremoteBrowseIr() {
    KremoteIrSession hw;
    // otherIRcodes / chooseCmdIrFile set returnToMenu; keep kremote open unless Home.
    const bool wasReturn = returnToMenu;
    String folder = kremoteBrowseStartFolder();
    otherIRcodes(folder.c_str());
    if (!forceHome) returnToMenu = wasReturn;
}
