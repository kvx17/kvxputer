#pragma once

#include "kremote_config.h"

class KremoteVertDisplayScope {
public:
    KremoteVertDisplayScope();
    ~KremoteVertDisplayScope();

private:
    int _savedRot = 0;
    bool _active = false;
};

void kremoteDrawFooter(const char *hints);
// Draw Virtual Remote: system top bar + pad + extra key bindings.
// present[KREMOTE_ACT_COUNT] marks which actions have a matching .ir command.
void kremoteDrawVirtualRemote(const char *remoteName, const bool present[KREMOTE_ACT_COUNT], int flashId,
                              int flashDigit);
void kremoteShowButtonMap();
