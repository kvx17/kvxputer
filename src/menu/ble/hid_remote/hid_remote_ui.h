#pragma once

#include "hid_remote_config.h"
#include "root/ui/display.h"
#include <vector>

void hidRemoteDrawHeader(HidRemoteTransport transport, bool connected, const char *modeLabel);
void hidRemoteDrawFooter(const char *hints = nullptr);
void hidRemoteDrawStatus(const char *line1, const char *line2 = nullptr);
int hidRemotePickFromList(const char *title, const std::vector<String> &labels, int startIndex = 0);
bool hidRemoteWaitBack();

// Display rotation scope for presenter vertical (restores on destroy)
class HidVertDisplayScope {
public:
    HidVertDisplayScope();
    ~HidVertDisplayScope();

private:
    int _savedRot = 0;
    bool _active = false;
};

// Draw a labeled key hint button; highlight when active
void hidDrawKeyBtn(int x, int y, int w, int h, const char *keyLabel, const char *desc, bool highlight);

// Presenter pad flash ids: 0-3 arrows, 4 space, 5 pgup, 6 pgdn, 7 home, 8 end, 9 play
void hidDrawPresenterPad(bool portrait, int flashId);

// Multimedia / movie layout flash ids (0-13)
void hidDrawMediaPad(int flashId);

// Mouse pad: flash 0-3 arrows, 4 L-click, 5 R-click, 6 wheel
void hidDrawMousePad(int flashId, bool joystickPresent = false);

// Keyboard FN layer overlay; flashKey is the physical key char or 0
void hidDrawKeyboardFnPad(int flashKey);

// Shorts pad: 0 up, 1 down, 2 play, 3 set. prompt may be "Press UP key" etc.
void hidDrawShortsPad(char upKey, char downKey, int flashId, const char *prompt = nullptr);

// Push-to-talk mic: talking = green, muted = red
void hidDrawPttPad(bool talking);

// Partial clear of content area (below header, above footer)
void hidClearContentArea();
