#include "pda_time.h"

#include "root/input/mykeyboard.h"
#include "root/ui/display.h"
#include <globals.h>

static const String ESC = String((char)0x1B);

struct tm pdaNow() {
#if defined(HAS_RTC)
    return _rtc.getTimeStruct();
#else
    return rtc.getTimeStruct();
#endif
}

bool pdaClockIsSet() { return clock_set; }

int pdaDaysInMonth(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 30;
    if (month == 2) {
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return days[month - 1];
}

// Sakamoto's algorithm: 0=Sunday .. 6=Saturday.
int pdaDayOfWeek(int year, int month, int day) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int y = year;
    if (month < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[month - 1] + day) % 7;
}

void pdaAddDays(int &year, int &month, int &day, int delta) {
    day += delta;
    while (day < 1) {
        month--;
        if (month < 1) {
            month = 12;
            year--;
        }
        day += pdaDaysInMonth(year, month);
    }
    while (day > pdaDaysInMonth(year, month)) {
        day -= pdaDaysInMonth(year, month);
        month++;
        if (month > 12) {
            month = 1;
            year++;
        }
    }
}

String pdaPad2(int value) {
    char b[8];
    snprintf(b, sizeof(b), "%02d", value);
    return String(b);
}

String pdaFormatDate(int year, int month, int day) {
    return String(year) + "-" + pdaPad2(month) + "-" + pdaPad2(day);
}

String pdaFormatTime(int hour, int minute) { return pdaPad2(hour) + ":" + pdaPad2(minute); }

bool pdaPromptDate(int &year, int &month, int &day, const char *title) {
    char def[9];
    snprintf(def, sizeof(def), "%04d%02d%02d", year, month, day);
    String r = num_keyboard(String(def), 8, title);
    if (r == ESC || r.length() != 8) return false;

    int ny = r.substring(0, 4).toInt();
    int nm = r.substring(4, 6).toInt();
    int nd = r.substring(6, 8).toInt();
    if (ny < 2000 || ny > 2099 || nm < 1 || nm > 12) {
        displayError("Invalid date (YYYYMMDD)", true);
        return false;
    }
    if (nd < 1 || nd > pdaDaysInMonth(ny, nm)) {
        displayError("Invalid day", true);
        return false;
    }
    year = ny;
    month = nm;
    day = nd;
    return true;
}

bool pdaPromptTime(int &hour, int &minute, const char *title) {
    char def[5];
    snprintf(def, sizeof(def), "%02d%02d", hour, minute);
    String r = num_keyboard(String(def), 4, title);
    if (r == ESC || r.length() != 4) return false;

    int nh = r.substring(0, 2).toInt();
    int nm = r.substring(2, 4).toInt();
    if (nh < 0 || nh > 23 || nm < 0 || nm > 59) {
        displayError("Invalid time (HHMM)", true);
        return false;
    }
    hour = nh;
    minute = nm;
    return true;
}

int pdaCompareDateTime(
    int y1, int mo1, int d1, int h1, int mi1, int y2, int mo2, int d2, int h2, int mi2
) {
    if (y1 != y2) return y1 < y2 ? -1 : 1;
    if (mo1 != mo2) return mo1 < mo2 ? -1 : 1;
    if (d1 != d2) return d1 < d2 ? -1 : 1;
    if (h1 != h2) return h1 < h2 ? -1 : 1;
    if (mi1 != mi2) return mi1 < mi2 ? -1 : 1;
    return 0;
}
