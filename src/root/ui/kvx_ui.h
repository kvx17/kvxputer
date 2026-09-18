#ifndef __KVX_UI_H__
#define __KVX_UI_H__

#include <globals.h>
#include <vector>

#define KVX_TOPBAR_H 24
#define KVX_PURPLE_DARK 0x600C

void drawKvxTopBar(const char *leftLabel);
// Optional mid/right status text (drawn left of the icon pack), e.g. selected WC city.
void drawKvxTopBar(const char *leftLabel, const char *statusLabel);
void drawKvxSubmenu(int index, std::vector<Option> &options, const char *title);
void kvxInvalidateSubmenuCache();

#endif
