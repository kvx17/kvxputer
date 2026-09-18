#include "pda_calendar.h"
#include "pda_alarms.h"
#include "pda_common.h"
#include "pda_editor.h"
#include "pda_time.h"

#include "root/input/mykeyboard.h"
#include "root/storage/paths.h"
#include "root/ui/display.h"
#include <globals.h>

// Storage format: one event per line as "YYYY-MM-DD|HH:MM|title".
static String eventsPath() { return String(kvx::paths::PDA_CALENDAR) + "/events.txt"; }

static const char *kMonthNames[] = {"January", "February", "March",     "April",   "May",      "June",
                                    "July",    "August",   "September", "October", "November", "December"};

static void pdaAddMonths(int &year, int &month, int &day, int delta) {
    month += delta;
    while (month < 1) {
        month += 12;
        year--;
    }
    while (month > 12) {
        month -= 12;
        year++;
    }
    int dim = pdaDaysInMonth(year, month);
    if (day > dim) day = dim;
}

static void pdaCalendarAddEvent(FS *fs, const String &date) {
    struct tm now = pdaNow();
    int hour = now.tm_hour, minute = now.tm_min;
    if (!pdaPromptTime(hour, minute, "Event time (HHMM):")) return;

    String title = "";
    if (pdaTextEditor(title, "Event title:", 80, false) != PDA_EDIT_OK || title.length() == 0) return;

    std::vector<String> lines = pdaReadLines(fs, eventsPath());
    lines.push_back(date + "|" + pdaFormatTime(hour, minute) + "|" + title);
    pdaWriteLines(fs, eventsPath(), lines);
    displaySuccess("Event added", true);
}

static void pdaCalendarDayView(FS *fs, int year, int month, int day) {
    String date = pdaFormatDate(year, month, day);

    while (true) {
        std::vector<String> all = pdaReadLines(fs, eventsPath());

        std::vector<size_t> matches;
        std::vector<String> labels;
        for (size_t i = 0; i < all.size(); i++) {
            if (!all[i].startsWith(date + "|")) continue;
            int p1 = all[i].indexOf('|');
            int p2 = all[i].indexOf('|', p1 + 1);
            String time = (p2 > p1) ? all[i].substring(p1 + 1, p2) : "";
            String title = (p2 >= 0) ? all[i].substring(p2 + 1) : "";
            matches.push_back(i);
            labels.push_back(time + "  " + title);
        }

        String heading = date + "  (" + String(matches.size()) + ")";

        std::vector<Option> opts;
        opts.push_back({"Add Event", [&]() { pdaCalendarAddEvent(fs, date); }});
        for (size_t k = 0; k < matches.size(); k++) {
            size_t lineIdx = matches[k];
            opts.push_back({labels[k], [&, lineIdx]() {
                                bool del = false;
                                std::vector<Option> sub = {
                                    {"Delete", [&]() { del = true; }},
                                };
                                loopOptions(sub, MENU_TYPE_SUBMENU, "Event");
                                if (del) {
                                    std::vector<String> cur = pdaReadLines(fs, eventsPath());
                                    if (lineIdx < cur.size()) {
                                        cur.erase(cur.begin() + lineIdx);
                                        pdaWriteLines(fs, eventsPath(), cur);
                                        displaySuccess("Deleted", true);
                                    }
                                }
                            }});
        }

        int r = loopOptions(opts, MENU_TYPE_SUBMENU, heading.c_str());
        if (r < 0 || returnToMenu || forceHome) break;
    }
}

static void pdaDrawCalendar(FS *fs, int year, int month, int curDay, const struct tm &now) {
    const uint16_t pri = kvxConfig.priColor;
    const uint16_t sec = kvxConfig.secColor;
    const uint16_t bg = kvxConfig.bgColor;

    int eventCount[32] = {0};
    String prefix = pdaFormatDate(year, month, 1).substring(0, 8); // "YYYY-MM-"
    for (const String &line : pdaReadLines(fs, eventsPath())) {
        if (line.startsWith(prefix)) {
            int d = line.substring(8, 10).toInt();
            if (d >= 1 && d <= 31) eventCount[d]++;
        }
    }

    tft.fillScreen(bg);

    tft.setTextColor(pri, bg);
    tft.setTextSize(FM);
    String header = String(kMonthNames[month - 1]) + " " + String(year);
    tft.drawCentreString(header, tftWidth / 2, 2, 1);

    const int margin = 4;
    const int gridX = margin;
    const int gridW = tftWidth - 2 * margin;
    const int cw = gridW / 7;

    const char *wd[] = {"S", "M", "T", "W", "T", "F", "S"};
    int wdY = 18;
    tft.setTextSize(FP);
    tft.setTextColor(sec, bg);
    for (int i = 0; i < 7; i++) tft.drawCentreString(wd[i], gridX + cw * i + cw / 2, wdY, 1);

    const int gridTop = 28;
    const int rows = 6;
    const int ch = (tftHeight - gridTop - 10) / rows;

    int firstDow = pdaDayOfWeek(year, month, 1); // 0=Sun
    int dim = pdaDaysInMonth(year, month);

    for (int day = 1; day <= dim; day++) {
        int cellIndex = firstDow + day - 1;
        int r = cellIndex / 7;
        int c = cellIndex % 7;
        int x = gridX + c * cw;
        int y = gridTop + r * ch;

        bool isToday =
            (now.tm_year + 1900 == year && now.tm_mon + 1 == month && now.tm_mday == day);
        bool isCursor = (day == curDay);

        if (isCursor) tft.fillRoundRect(x + 1, y + 1, cw - 2, ch - 2, 3, sec);
        else if (isToday) tft.drawRoundRect(x + 1, y + 1, cw - 2, ch - 2, 3, sec);

        tft.setTextColor(isCursor ? bg : pri, isCursor ? sec : bg);
        tft.drawCentreString(String(day), x + cw / 2, y + 2, 1);

        if (eventCount[day] > 0) tft.fillCircle(x + cw / 2, y + ch - 3, 1, isCursor ? bg : sec);
    }

    tft.setTextColor(sec, bg);
    tft.drawCentreString("Fn+;/. year  [] month  arrows day", tftWidth / 2, tftHeight - 9, 1);
}

void pdaCalendar() {
    FS *fs = nullptr;
    if (!pdaGetFs(fs)) return;
    kvx::paths::ensureDir(*fs, kvx::paths::PDA_CALENDAR);

    struct tm now = pdaNow();
    int cy = now.tm_year + 1900;
    int cm = now.tm_mon + 1;
    int cd = now.tm_mday;
    if (cy < 2000) { // clock not set -> start somewhere sane
        cy = 2025;
        cm = 1;
        cd = 1;
    }

    bool redraw = true;
    while (true) {
        if (pdaAlarmsPoll()) redraw = true;

        if (redraw) {
            now = pdaNow();
            pdaDrawCalendar(fs, cy, cm, cd, now);
            redraw = false;
        }

        if (check(EscPress)) break;

        bool monthLeft = false, monthRight = false;
        bool yearUp = false, yearDown = false;
#ifdef HAS_KEYBOARD
        keyStroke key = _getKeyPress();
        if (key.pressed) {
            for (char raw : key.word) {
                unsigned char c = (unsigned char)raw;
                if (c == '[' || c == 0xD8) monthLeft = true;
                else if (c == ']' || c == 0xD7) monthRight = true;
                // Fn+; / Fn+. → HID up/down arrows → jump ±1 year
                else if (c == 0xDA) yearUp = true;
                else if (c == 0xD9) yearDown = true;
            }
        }
#else
        keyStroke key;
#endif

        if (yearUp) {
            check(UpPress);
            pdaAddMonths(cy, cm, cd, -12);
            redraw = true;
        } else if (yearDown) {
            check(DownPress);
            pdaAddMonths(cy, cm, cd, +12);
            redraw = true;
        } else if (monthLeft) {
            check(PrevPress);
            pdaAddMonths(cy, cm, cd, -1);
            redraw = true;
        } else if (monthRight) {
            check(NextPress);
            pdaAddMonths(cy, cm, cd, +1);
            redraw = true;
        } else if (check(UpPress)) {
            pdaAddDays(cy, cm, cd, -7);
            redraw = true;
        } else if (check(DownPress)) {
            pdaAddDays(cy, cm, cd, +7);
            redraw = true;
        } else if (check(PrevPress)) {
            pdaAddDays(cy, cm, cd, -1);
            redraw = true;
        } else if (check(NextPress)) {
            pdaAddDays(cy, cm, cd, +1);
            redraw = true;
        } else if (check(SelPress) || (key.pressed && key.enter)) {
            check(SelPress);
            pdaCalendarDayView(fs, cy, cm, cd);
            redraw = true;
            if (returnToMenu || forceHome) break;
        }

        delay(20);
    }

    tft.fillScreen(kvxConfig.bgColor);
}
