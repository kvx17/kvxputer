#ifndef __KVX_MAIN_MENU_H__
#define __KVX_MAIN_MENU_H__

#include <MenuItemInterface.h>
#include <vector>

// Wii-style channel grid (Cardputer/StickS3: 2×3; Tab5: 3×6).
int kvxMainMenuLoop(std::vector<MenuItemInterface *> &items, int startIndex = 0);

#if defined(HAS_TOUCH)
// Tab5 (and other touch boards): gesture routing helpers while the channel grid is open.
bool kvxMainMenuActive(void);
int kvxMainMenuIndexAt(int x, int y);
void kvxMainMenuSelectIndex(int index);
#endif

#endif
