#ifndef __CLOCK_FACES_H__
#define __CLOCK_FACES_H__

#include <Arduino.h>
#include <time.h>

// Shared clock / charge-style face painters (no input loops).

void clockFaceFormatDate(const struct tm &t, char *out, size_t outLen);
void clockFaceDrawMonthCalendar(
    int x, int y, int w, int h, const struct tm &t, uint16_t color, uint16_t bg
);
void clockFaceDrawChargeBar(int x, int y, int w, int h, int percent, uint16_t col, uint16_t bg);

// Charge-style face: date, large clock, %, optional calendar, battery bar.
// Draws into the body below topY (typically KVX_TOPBAR_H+1).
void clockFaceDrawChargeStyle(
    int topY, const struct tm &t, const char *timeStr, int batteryPct, uint16_t color, bool showCalendar
);

// Classic digital clock face (centered auto-sized timeStr).
void clockFaceDrawDigital(int topY, const char *timeStr, uint16_t color);

#endif
