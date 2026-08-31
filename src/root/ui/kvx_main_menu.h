#ifndef __KVX_MAIN_MENU_H__
#define __KVX_MAIN_MENU_H__

#include <MenuItemInterface.h>
#include <vector>

// Wii-style 2×3 channel grid (6 icons per page).
int kvxMainMenuLoop(std::vector<MenuItemInterface *> &items, int startIndex = 0);

void kvxApplyThemeDefaults();

#endif
