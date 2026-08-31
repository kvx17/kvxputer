#ifndef __UNIT_SCROLL_H__
#define __UNIT_SCROLL_H__

#include <Arduino.h>

// Optional M5 Unit Scroll (I2C 0x40) on Grove PORT.A.
// Safe no-ops when UNIT_SCROLL is not defined at build time.

bool unitScrollIsPresent();
bool unitScrollGroveBusy(); // true when Scroll claimed PORT.A (RF SPI conflict)
bool unitScrollBegin(bool quiet = true);
bool unitScrollReconnect();
void unitScrollPoll();      // call from pollEncoder()
void unitScrollApplyInput(); // OR pending events into Prev/Next/Sel/AnyKey

String unitScrollStatusLabel();

#endif
