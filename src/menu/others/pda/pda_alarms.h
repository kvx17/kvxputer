#ifndef __PDA_ALARMS_H__
#define __PDA_ALARMS_H__

// PDA > Alarms / Schedule Keeper: one-shot datetime alarms with an audio alert.
void pdaAlarms();

// Cheap due-check meant to be called from an idle path (the main menu loop).
// Fires (and marks) any past-due, untriggered alarms. No-op if the clock is
// unset or no storage is available. Returns true if an alarm was shown (so the
// caller can repaint its screen).
bool pdaAlarmsCheckDue();

// Rate-limited wrapper around pdaAlarmsCheckDue() (one check per 15s, shared
// timer). Call from editor / calendar / world-clock / loopOptions idle paths.
bool pdaAlarmsPoll();

#endif
