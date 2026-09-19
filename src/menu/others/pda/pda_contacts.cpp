#include "pda_contacts.h"
#include "pda_alarms.h"
#include "pda_common.h"
#include "pda_editor.h"

#include "root/input/mykeyboard.h"
#include "root/storage/paths.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#include "root/ui/theme.h"
#include <algorithm>
#include <globals.h>

// Storage format: one contact per line as "name|phone|address".
struct Contact {
    String name;
    String phone;
    String address;
};

static String contactsPath() { return String(kvx::paths::PDA_CONTACTS) + "/contacts.txt"; }

static Contact parseContact(const String &line) {
    Contact c;
    int p1 = line.indexOf('|');
    int p2 = line.indexOf('|', p1 + 1);
    if (p1 < 0) {
        c.name = line;
        return c;
    }
    c.name = line.substring(0, p1);
    if (p2 < 0) {
        c.phone = line.substring(p1 + 1);
        return c;
    }
    c.phone = line.substring(p1 + 1, p2);
    c.address = line.substring(p2 + 1);
    return c;
}

static String serializeContact(const Contact &c) { return c.name + "|" + c.phone + "|" + c.address; }

static void writeContacts(FS *fs, const std::vector<Contact> &contacts) {
    std::vector<String> lines;
    for (const Contact &c : contacts) lines.push_back(serializeContact(c));
    pdaWriteLines(fs, contactsPath(), lines);
}

static std::vector<Contact> loadContacts(FS *fs) {
    std::vector<Contact> contacts;
    for (const String &l : pdaReadLines(fs, contactsPath())) {
        if (l.length()) contacts.push_back(parseContact(l));
    }
    std::sort(contacts.begin(), contacts.end(), [](const Contact &a, const Contact &b) {
        String ua = a.name, ub = b.name;
        ua.toUpperCase();
        ub.toUpperCase();
        return ua < ub;
    });
    return contacts;
}

static bool nameMatches(const String &name, const String &filter) {
    if (!filter.length()) return true;
    String u = name;
    String f = filter;
    u.toUpperCase();
    f.toUpperCase();
    return u.indexOf(f) >= 0;
}

static void pdaNewContact(FS *fs, std::vector<Contact> &contacts) {
    Contact c;
    String name = "";
    if (pdaTextEditor(name, "Name:", 60, false) != PDA_EDIT_OK || name.length() == 0) return;
    c.name = name;
    String phone = "";
    if (pdaTextEditor(phone, "Phone:", 40, false) == PDA_EDIT_OK) c.phone = phone;
    String addr = "";
    if (pdaTextEditor(addr, "Address:", 120, false) == PDA_EDIT_OK) c.address = addr;
    contacts.push_back(c);
    writeContacts(fs, contacts);
}

static char contactInitial(const Contact &c) {
    if (!c.name.length()) return '?';
    char ch = c.name[0];
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    return ch;
}

static void buildContactBodyRows(const Contact &c, int fieldMax, std::vector<String> &rows) {
    rows.clear();
    auto appendField = [&](const char *label, const String &value) {
        String v = value.length() ? value : String("-");
        String first = String(label) + ": " + v;
        if ((int)first.length() <= fieldMax) {
            rows.push_back(first);
            return;
        }
        // Keep "Label: " on the first line with as much value as fits.
        String prefix = String(label) + ": ";
        int room = fieldMax - (int)prefix.length();
        if (room < 4) {
            rows.push_back(String(label) + ":");
            room = fieldMax;
            prefix = "";
        }
        int i = 0;
        rows.push_back(prefix + v.substring(0, room));
        i = room;
        while (i < (int)v.length()) {
            rows.push_back(v.substring(i, min(i + fieldMax, (int)v.length())));
            i += fieldMax;
        }
    };
    appendField("Phone", c.phone);
    appendField("Address", c.address);
}

static int contactCardMaxScroll(const Contact &c) {
    const int cardW = tftWidth - 16;
    const int cardH = tftHeight - (KVX_TOPBAR_H + 4) - 16;
    const int fieldMax = max(8, (cardW - 20) / uiCharW(FP));
    std::vector<String> rows;
    buildContactBodyRows(c, fieldMax, rows);
    const int headerH = uiLineH(FM) + 12;
    const int footerReserve = uiLineH(FP) + 4;
    const int bodyH = cardH - headerH - footerReserve;
    const int rowH = uiLineH(FP) + 2;
    const int maxVisible = max(1, bodyH / rowH);
    return max(0, (int)rows.size() - maxVisible);
}

static void drawContactCard(const Contact &c, int scroll) {
    TftFrame frame;
    const uint16_t bg = kvxConfig.bgColor;
    const uint16_t pri = kvxConfig.priColor;
    const uint16_t sec = kvxConfig.secColor;
    const uint16_t cardBg = getColorVariation(pri, 14, -1);

    tft.fillScreen(bg);
    drawKvxTopBar("Contact");

    const int cardX = 8;
    const int cardY = KVX_TOPBAR_H + 4;
    const int cardW = tftWidth - 16;
    const int cardH = tftHeight - cardY - 16;
    tft.fillRoundRect(cardX, cardY, cardW, cardH, 8, cardBg);
    tft.drawRoundRect(cardX, cardY, cardW, cardH, 8, sec);

    const int cx = cardX + 18;
    const int cy = cardY + uiLineH(FM) / 2 + 6;
    const int avR = uiLineH(FM) / 2 + 2;
    tft.fillCircle(cx, cy, avR, sec);
    tft.setTextSize(FM);
    tft.setTextColor(bg, sec);
    char init[2] = {contactInitial(c), 0};
    tft.drawCentreString(init, cx, cy - uiLineH(FM) / 2, 1);

    tft.setTextSize(FM);
    tft.setTextColor(pri, cardBg);
    String name = c.name.length() ? c.name : String("(no name)");
    int nameMax = max(1, (cardW - 50) / uiCharW(FM));
    if ((int)name.length() > nameMax) name = name.substring(0, nameMax);
    tft.setCursor(cardX + 42, cardY + 6);
    tft.print(name);

    // FP body so Phone + number share one line and both fields fit the card.
    const int fieldMax = max(8, (cardW - 20) / uiCharW(FP));
    std::vector<String> rows;
    buildContactBodyRows(c, fieldMax, rows);

    const int headerH = uiLineH(FM) + 12;
    const int bodyY = cardY + headerH;
    const int footerReserve = uiLineH(FP) + 4;
    const int bodyH = cardH - headerH - footerReserve;
    const int rowH = uiLineH(FP) + 2;
    const int maxVisible = max(1, bodyH / rowH);
    int maxScroll = max(0, (int)rows.size() - maxVisible);
    if (scroll < 0) scroll = 0;
    if (scroll > maxScroll) scroll = maxScroll;

    tft.fillRect(cardX + 2, bodyY, cardW - 4, bodyH, cardBg);
    for (int i = 0; i < maxVisible && scroll + i < (int)rows.size(); i++) {
        const String &row = rows[scroll + i];
        int y = bodyY + i * rowH;
        tft.setTextSize(FP);
        tft.setTextColor(pri, cardBg);
        tft.setCursor(cardX + 10, y);
        tft.print(row);
    }

    tft.setTextSize(FP);
    tft.setTextColor(sec, bg);
    tft.drawCentreString(
        maxScroll > 0 ? "Arrows scroll  E edit  D del  ESC" : "E edit  D delete  ESC back",
        tftWidth / 2,
        uiFooterY(FP),
        1
    );
}

static void pdaContactCard(FS *fs, std::vector<Contact> &contacts, size_t index) {
    while (true) {
        if (index >= contacts.size() || returnToMenu || forceHome) break;
        Contact &c = contacts[index];
        int scroll = 0;
        drawContactCard(c, scroll);

        bool done = false;
        while (!done) {
            if (returnToMenu || forceHome) return;
            if (pdaAlarmsPoll()) {
                drawContactCard(c, scroll);
            }
            if (check(EscPress)) return;

            if (check(UpPress) || check(PrevPress)) {
                if (scroll > 0) {
                    scroll--;
                    drawContactCard(c, scroll);
                }
            } else if (check(DownPress) || check(NextPress)) {
                if (scroll < contactCardMaxScroll(c)) {
                    scroll++;
                    drawContactCard(c, scroll);
                }
            }

#ifdef HAS_KEYBOARD
            keyStroke key = _getKeyPress();
            if (key.pressed) {
                for (char raw : key.word) {
                    char ch = raw;
                    if (ch == 'e' || ch == 'E') {
                        std::vector<Option> opts = {
                            {"Edit Name",
                             [&]() {
                                 String v = c.name;
                                 if (pdaTextEditor(v, "Name:", 60, false) == PDA_EDIT_OK && v.length()) {
                                     c.name = v;
                                     writeContacts(fs, contacts);
                                 }
                             }},
                            {"Edit Phone",
                             [&]() {
                                 String v = c.phone;
                                 if (pdaTextEditor(v, "Phone:", 40, false) == PDA_EDIT_OK) {
                                     c.phone = v;
                                     writeContacts(fs, contacts);
                                 }
                             }},
                            {"Edit Address",
                             [&]() {
                                 String v = c.address;
                                 if (pdaTextEditor(v, "Address:", 120, false) == PDA_EDIT_OK) {
                                     c.address = v;
                                     writeContacts(fs, contacts);
                                 }
                             }},
                            {"Back", []() {}},
                        };
                        loopOptions(opts, MENU_TYPE_SUBMENU, "Edit Contact");
                        done = true;
                        break;
                    }
                    if (ch == 'd' || ch == 'D') {
                        int8_t choice =
                            displayMessage("Delete contact?", "No", nullptr, "Yes", TFT_RED);
                        if (choice == 1) {
                            contacts.erase(contacts.begin() + index);
                            writeContacts(fs, contacts);
                            return;
                        }
                        done = true;
                        break;
                    }
                }
                if (key.enter) {
                    check(SelPress);
                    // Sel opens edit menu on keyboard boards too.
                    std::vector<Option> opts = {
                        {"Edit Name",
                         [&]() {
                             String v = c.name;
                             if (pdaTextEditor(v, "Name:", 60, false) == PDA_EDIT_OK && v.length()) {
                                 c.name = v;
                                 writeContacts(fs, contacts);
                             }
                         }},
                        {"Edit Phone",
                         [&]() {
                             String v = c.phone;
                             if (pdaTextEditor(v, "Phone:", 40, false) == PDA_EDIT_OK) {
                                 c.phone = v;
                                 writeContacts(fs, contacts);
                             }
                         }},
                        {"Edit Address",
                         [&]() {
                             String v = c.address;
                             if (pdaTextEditor(v, "Address:", 120, false) == PDA_EDIT_OK) {
                                 c.address = v;
                                 writeContacts(fs, contacts);
                             }
                         }},
                        {"Delete",
                         [&]() {
                             contacts.erase(contacts.begin() + index);
                             writeContacts(fs, contacts);
                             done = true;
                         }},
                        {"Back", []() {}},
                    };
                    int r = loopOptions(opts, MENU_TYPE_SUBMENU, "Contact");
                    if (r < 0 || returnToMenu || forceHome) return;
                    if (index >= contacts.size()) return;
                    done = true;
                    break;
                }
            }
#else
            if (check(SelPress)) {
                std::vector<Option> opts = {
                    {"Edit Name",
                     [&]() {
                         String v = c.name;
                         if (pdaTextEditor(v, "Name:", 60, false) == PDA_EDIT_OK && v.length()) {
                             c.name = v;
                             writeContacts(fs, contacts);
                         }
                     }},
                    {"Edit Phone",
                     [&]() {
                         String v = c.phone;
                         if (pdaTextEditor(v, "Phone:", 40, false) == PDA_EDIT_OK) {
                             c.phone = v;
                             writeContacts(fs, contacts);
                         }
                     }},
                    {"Edit Address",
                     [&]() {
                         String v = c.address;
                         if (pdaTextEditor(v, "Address:", 120, false) == PDA_EDIT_OK) {
                             c.address = v;
                             writeContacts(fs, contacts);
                         }
                     }},
                    {"Delete",
                     [&]() {
                         contacts.erase(contacts.begin() + index);
                         writeContacts(fs, contacts);
                         done = true;
                     }},
                    {"Back", []() {}},
                };
                int r = loopOptions(opts, MENU_TYPE_SUBMENU, "Contact");
                if (r < 0 || returnToMenu || forceHome) return;
                if (index >= contacts.size()) return;
                done = true;
            }
#endif
            delay(20);
        }
    }
}

#ifdef HAS_KEYBOARD

static bool isNavPunct(char c) { return c == ';' || c == ',' || c == '.' || c == '/'; }

static void pdaContactsKeyboard(FS *fs) {
    String filter = "";
    int highlight = 0;
    bool redraw = true;
    std::vector<Contact> contacts = loadContacts(fs);

    tft.fillScreen(kvxConfig.bgColor);

    for (;;) {
        if (returnToMenu || forceHome) break;
        if (pdaAlarmsPoll()) redraw = true;

        std::vector<size_t> matches;
        for (size_t i = 0; i < contacts.size(); i++) {
            if (nameMatches(contacts[i].name, filter)) matches.push_back(i);
        }
        const int rowCount = 1 + (int)matches.size();
        if (highlight < 0) highlight = 0;
        if (highlight >= rowCount) highlight = rowCount - 1;

        if (redraw) {
            tft.fillScreen(kvxConfig.bgColor);
            drawKvxTopBar("Contacts");
            tft.setTextSize(FP);
            tft.setTextColor(kvxConfig.secColor, kvxConfig.bgColor);
            String sub = filter.length() ? ("Filter: " + filter) : String("Type to search");
            tft.setCursor(BORDER_PAD_X, KVX_TOPBAR_H + 4);
            tft.print(sub);

            const int listY = KVX_TOPBAR_H + FP * LH + 8;
            // Single-line rows so more than one contact fits on 135px (was dual-line → maxRows≈1).
            const int rowH = uiRowH(FP);
            const int footerY = uiFooterY(FP);
            const int maxRows = max(1, (footerY - listY) / rowH);
            int start = highlight - maxRows + 1;
            if (start < 0) start = 0;

            for (int r = 0; r < maxRows && start + r < rowCount; r++) {
                int idx = start + r;
                int y = listY + r * rowH;
                bool sel = (idx == highlight);
                if (sel) {
                    tft.fillRect(
                        BORDER_PAD_X - 2, y - 1, tftWidth - 2 * BORDER_PAD_X + 4, rowH, kvxConfig.secColor
                    );
                    tft.setTextColor(kvxConfig.bgColor, kvxConfig.secColor);
                } else {
                    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
                }
                tft.setTextSize(FP);
                tft.setCursor(BORDER_PAD_X, y);
                if (idx == 0) {
                    tft.print("New Contact");
                } else {
                    const Contact &ct = contacts[matches[idx - 1]];
                    String line = ct.name;
                    if (ct.phone.length()) line += "  " + ct.phone;
                    int maxN = max(1, (tftWidth - 2 * BORDER_PAD_X) / uiCharW(FP));
                    if ((int)line.length() > maxN) line = line.substring(0, maxN);
                    tft.print(line);
                }
            }
            tft.setTextSize(FP);
            tft.setTextColor(kvxConfig.secColor, kvxConfig.bgColor);
            tft.drawCentreString("type=search  OK=open  ESC=back", tftWidth / 2, footerY, 1);
            redraw = false;
        }

        keyStroke key = _getKeyPress();
        bool hidUp = false, hidDown = false;
        bool skipUp = false, skipDown = false;
        bool reload = false;

        if (key.pressed) {
            if (key.del && filter.length()) {
                filter.remove(filter.length() - 1);
                highlight = 0;
                redraw = true;
            }
            if (key.enter) {
                check(SelPress);
                if (highlight == 0) pdaNewContact(fs, contacts);
                else if (highlight > 0 && highlight - 1 < (int)matches.size())
                    pdaContactCard(fs, contacts, matches[highlight - 1]);
                reload = true;
            }
            for (char raw : key.word) {
                unsigned char c = (unsigned char)raw;
                if (c == 0xDA) {
                    hidUp = true;
                    continue;
                }
                if (c == 0xD9) {
                    hidDown = true;
                    continue;
                }
                if (c == 0xB1 || c == '`') continue;
                if (c < 32 || c > 126) continue;
                if (c == ':') skipUp = true;
                else if (c == '>') skipDown = true;
                if (isNavPunct((char)c) && (UpPress || DownPress || PrevPress || NextPress)) continue;
                if (filter.length() < 40) filter += (char)c;
                highlight = 0;
                redraw = true;
            }
        }

        if (check(EscPress)) {
            if (filter.length()) {
                filter = "";
                highlight = 0;
                redraw = true;
            } else {
                break;
            }
        }
        if (!key.enter && check(SelPress)) {
            if (highlight == 0) pdaNewContact(fs, contacts);
            else if (highlight > 0 && highlight - 1 < (int)matches.size())
                pdaContactCard(fs, contacts, matches[highlight - 1]);
            reload = true;
        }

        if (check(UpPress)) {
            if (!skipUp) hidUp = true;
        }
        if (check(PrevPress)) {
            if (!skipUp) hidUp = true;
        }
        if (check(DownPress)) {
            if (!skipDown) hidDown = true;
        }
        if (check(NextPress)) {
            if (!skipDown) hidDown = true;
        }
        if (hidUp && highlight > 0) {
            highlight--;
            redraw = true;
        }
        if (hidDown && highlight < rowCount - 1) {
            highlight++;
            redraw = true;
        }

        if (reload) {
            contacts = loadContacts(fs);
            redraw = true;
        }

        delay(20);
    }

    tft.fillScreen(kvxConfig.bgColor);
}

#endif // HAS_KEYBOARD

void pdaContacts() {
    FS *fs = nullptr;
    if (!pdaGetFs(fs)) return;
    kvx::paths::ensureDir(*fs, kvx::paths::PDA_CONTACTS);

#ifdef HAS_KEYBOARD
    pdaContactsKeyboard(fs);
#else
    String filter = "";

    while (true) {
        std::vector<Contact> contacts = loadContacts(fs);

        std::vector<Option> opts;
        opts.push_back({"New Contact", [&]() { pdaNewContact(fs, contacts); }});
        opts.push_back({filter.length() ? ("Search: " + filter) : String("Search"), [&]() {
                            String q = filter;
                            if (pdaTextEditor(q, "Search name:", 40, false) == PDA_EDIT_OK) filter = q;
                        }});

        for (size_t i = 0; i < contacts.size(); i++) {
            if (!nameMatches(contacts[i].name, filter)) continue;
            String label = contacts[i].name;
            if (contacts[i].phone.length()) label += "  " + contacts[i].phone;
            opts.push_back({label, [&, i]() { pdaContactCard(fs, contacts, i); }});
        }

        int r = loopOptions(opts, MENU_TYPE_SUBMENU, "Contacts");
        if (r < 0 || returnToMenu || forceHome) break;
    }
#endif
}
