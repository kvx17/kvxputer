#pragma once

#include <Arduino.h>
#include <FS.h>

/// Browse WD CSV captures and show a table of WiFi APs.
void wardrivingViewCaptures();

/// Open one capture file (table view). Used from session summary too.
void wardrivingViewCaptureFile(FS &fs, const String &path, const String &name);
