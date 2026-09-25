#pragma once

#include <Arduino.h>
#include <FS.h>

void kremoteUseFlow();
void kremoteFavoritesFlow();
void kremoteDeleteFlow();
void kremoteBrowseIr();

// Virtual Remote pad for a loaded .ir path (fixed Cardputer key map).
void kremoteVirtualRemote(FS *fs, const String &filepath, const String &displayName);

void kremoteBeginIrHw();
void kremoteEndIrHw();
int kremoteLearnRxPin();
String kremoteBrowseStartFolder();
const char *kremoteIrHwLabel();
