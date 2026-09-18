#include "pda_alarms.h"
#include "pda_common.h"
#include "pda_editor.h"
#include "pda_time.h"

#include "menu/others/audio.h"
#include "root/storage/paths.h"
#include "root/ui/display.h"
#include <globals.h>

// Storage format: one alarm per line as "triggered|YYYY-MM-DD|HH:MM|label".
struct Alarm {
    bool triggered;
    int year, month, day, hour, minute;
    String label;
};

static String alarmsPath() { return String(kvx::paths::PDA_ALARMS) + "/alarms.txt"; }

static bool parseAlarm(const String &line, Alarm &a) {
    int p1 = line.indexOf('|');
    int p2 = line.indexOf('|', p1 + 1);
    int p3 = line.indexOf('|', p2 + 1);
    if (p1 < 0 || p2 < 0 || p3 < 0) return false;

    a.triggered = line.substring(0, p1) == "1";
    String date = line.substring(p1 + 1, p2);
    String time = line.substring(p2 + 1, p3);
    a.label = line.substring(p3 + 1);

    if (date.length() < 10 || time.length() < 5) return false;
    a.year = date.substring(0, 4).toInt();
    a.month = date.substring(5, 7).toInt();
    a.day = date.substring(8, 10).toInt();
    a.hour = time.substring(0, 2).toInt();
    a.minute = time.substring(3, 5).toInt();
    return true;
}

static String serializeAlarm(const Alarm &a) {
    return String(a.triggered ? "1" : "0") + "|" + pdaFormatDate(a.year, a.month, a.day) + "|" +
           pdaFormatTime(a.hour, a.minute) + "|" + a.label;
}

static std::vector<Alarm> loadAlarms(FS *fs) {
    std::vector<Alarm> alarms;
    for (const String &l : pdaReadLines(fs, alarmsPath())) {
        Alarm a;
        if (l.length() && parseAlarm(l, a)) alarms.push_back(a);
    }
    return alarms;
}

static void writeAlarms(FS *fs, const std::vector<Alarm> &alarms) {
    std::vector<String> lines;
    for (const Alarm &a : alarms) lines.push_back(serializeAlarm(a));
    pdaWriteLines(fs, alarmsPath(), lines);
}

static String alarmLabel(const Alarm &a) {
    String s = pdaFormatDate(a.year, a.month, a.day) + " " + pdaFormatTime(a.hour, a.minute);
    if (a.label.length()) s += " " + a.label;
    if (a.triggered) s += " [done]";
    return s;
}

static void playAlarmSound() {
#if defined(BUZZ_PIN) || defined(HAS_NS4168_SPKR)
    for (int i = 0; i < 4; i++) {
        _tone(2500, 200);
        delay(120);
    }
#endif
}

static void pdaAlarmActions(FS *fs, std::vector<Alarm> &alarms, size_t index) {
    bool done = false;
    while (true) {
        Alarm &a = alarms[index];
        std::vector<Option> opts = {
            {"Set Date", [&]() {
                 if (pdaPromptDate(a.year, a.month, a.day, "Date (YYYYMMDD):")) {
                     a.triggered = false;
                     writeAlarms(fs, alarms);
                 }
             }},
            {"Set Time", [&]() {
                 if (pdaPromptTime(a.hour, a.minute, "Time (HHMM):")) {
                     a.triggered = false;
                     writeAlarms(fs, alarms);
                 }
             }},
            {"Edit Label", [&]() {
                 String v = a.label;
                 if (pdaTextEditor(v, "Label:", 80, false) == PDA_EDIT_OK) {
                     a.label = v;
                     writeAlarms(fs, alarms);
                 }
             }},
            {a.triggered ? "Re-arm" : "Disarm", [&]() {
                 a.triggered = !a.triggered;
                 writeAlarms(fs, alarms);
             }},
            {"Delete", [&]() {
                 alarms.erase(alarms.begin() + index);
                 writeAlarms(fs, alarms);
                 done = true;
             }},
        };
        int r = loopOptions(opts, MENU_TYPE_SUBMENU, "Alarm");
        if (r < 0 || done || returnToMenu || forceHome) break;
    }
}

void pdaAlarms() {
    FS *fs = nullptr;
    if (!pdaGetFs(fs)) return;
    kvx::paths::ensureDir(*fs, kvx::paths::PDA_ALARMS);

    while (true) {
        std::vector<Alarm> alarms = loadAlarms(fs);

        std::vector<Option> opts;
        opts.push_back({"New Alarm", [&]() {
                            struct tm now = pdaNow();
                            Alarm a;
                            a.triggered = false;
                            a.year = now.tm_year + 1900;
                            a.month = now.tm_mon + 1;
                            a.day = now.tm_mday;
                            a.hour = now.tm_hour;
                            a.minute = now.tm_min;
                            if (a.year < 2000) {
                                a.year = 2025;
                                a.month = 1;
                                a.day = 1;
                            }
                            if (!pdaPromptDate(a.year, a.month, a.day, "Date (YYYYMMDD):")) return;
                            if (!pdaPromptTime(a.hour, a.minute, "Time (HHMM):")) return;
                            String label = "";
                            if (pdaTextEditor(label, "Label:", 80, false) != PDA_EDIT_OK) label = "";
                            a.label = label;
                            alarms.push_back(a);
                            writeAlarms(fs, alarms);
                        }});
        for (size_t i = 0; i < alarms.size(); i++) {
            opts.push_back({alarmLabel(alarms[i]), [&, i]() { pdaAlarmActions(fs, alarms, i); }});
        }

        if (!pdaClockIsSet()) {
            opts.push_back({"(Clock not set - alarms won't fire)", []() {}, false, nullptr, nullptr,
                            false, false});
        }

        int r = loopOptions(opts, MENU_TYPE_SUBMENU, "Alarms");
        if (r < 0 || returnToMenu || forceHome) break;
    }
}

bool pdaAlarmsCheckDue() {
    if (!clock_set) return false;

    FS *fs = nullptr;
    if (!getFsStorage(fs) || fs == nullptr) return false;
    if (!fs->exists(alarmsPath())) return false;

    std::vector<Alarm> alarms = loadAlarms(fs);
    if (alarms.empty()) return false;

    struct tm now = pdaNow();
    int ny = now.tm_year + 1900, nmo = now.tm_mon + 1, nd = now.tm_mday;
    int nh = now.tm_hour, nmi = now.tm_min;

    String fired = "";
    bool changed = false;
    for (Alarm &a : alarms) {
        if (a.triggered) continue;
        int cmp = pdaCompareDateTime(a.year, a.month, a.day, a.hour, a.minute, ny, nmo, nd, nh, nmi);
        if (cmp <= 0) {
            a.triggered = true;
            changed = true;
            if (fired.length()) fired += "\n";
            fired += pdaFormatTime(a.hour, a.minute) + " " + a.label;
        }
    }

    if (changed) {
        writeAlarms(fs, alarms);
        wakeUpScreen();
        previousMillis = millis();
        playAlarmSound();
        displayWarning("Alarm!\n" + fired, true);
        previousMillis = millis();
    }
    return changed;
}

bool pdaAlarmsPoll() {
    static unsigned long last = 0;
    if (millis() - last < 15000) return false;
    last = millis();
    return pdaAlarmsCheckDue();
}
