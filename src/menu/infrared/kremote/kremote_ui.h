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

void kremoteDrawHeader(const char *title);
void kremoteDrawFooter(const char *hints);
void kremoteClearContent();
void kremoteDrawPad(bool portrait, bool swapped, int flashId);
void kremoteShowButtonMap();
