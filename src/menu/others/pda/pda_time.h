#ifndef __PDA_TIME_H__
#define __PDA_TIME_H__

#include <Arduino.h>
#include <time.h>

// Shared date/time helpers for the PDA calendar, alarms and world clock.
// Time is read from the firmware's software RTC (ESP32Time `rtc`).

struct tm pdaNow();                       // current local time
bool pdaClockIsSet();                     // true once the user has set the clock

int pdaDaysInMonth(int year, int month);  // month 1-12
int pdaDayOfWeek(int year, int month, int day); // 0=Sunday .. 6=Saturday
void pdaAddDays(int &year, int &month, int &day, int delta); // rolls across months

String pdaPad2(int value);
String pdaFormatDate(int year, int month, int day); // "YYYY-MM-DD"
String pdaFormatTime(int hour, int minute);         // "HH:MM"

// Numeric-keyboard prompts, pre-filled with the passed-in values. Return false
// on cancel or invalid input (an error is shown for invalid values).
bool pdaPromptDate(int &year, int &month, int &day, const char *title);
bool pdaPromptTime(int &hour, int &minute, const char *title);

// Compare two (Y,M,D,h,m) tuples: -1 if a<b, 0 if equal, 1 if a>b.
int pdaCompareDateTime(
    int y1, int mo1, int d1, int h1, int mi1, int y2, int mo2, int d2, int h2, int mi2
);

#endif
