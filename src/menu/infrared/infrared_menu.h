#ifndef __IR_MENU_H__
#define __IR_MENU_H__

#include <MenuItemInterface.h>

class IRMenu : public MenuItemInterface {
public:
    IRMenu() : MenuItemInterface("IR") {}

    void optionsMenu(void);
    void configMenu(void);
    void drawIcon(float scale);
    bool hasTheme() { return kvxConfig.theme.ir; }
    const String& themePath() override { return kvxConfig.theme.paths.ir; }
};

#endif
