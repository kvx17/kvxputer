#ifndef __PDA_EDITOR_H__
#define __PDA_EDITOR_H__

#include <Arduino.h>

// Full-screen caret editor on HAS_KEYBOARD boards (Cardputer, T-Deck, …).
// Boards without a physical keyboard fall back to the on-screen keyboard().
//
// On cancel the buffer is left unchanged and PDA_EDIT_CANCEL is returned
// (ASCII ESC, matching keyboard()). On save, `text` is updated in place.

enum { PDA_EDIT_OK = 0, PDA_EDIT_CANCEL = 1 };

int pdaTextEditor(String &text, const char *title, int maxLen, bool multiline);

#endif
