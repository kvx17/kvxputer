#ifndef __RF_MENU_H__
#define __RF_MENU_H__

#include <MenuItemInterface.h>

class RFMenu : public MenuItemInterface {
public:
    RFMenu() : MenuItemInterface("RF") {}

    void optionsMenu(void);
    void drawIcon(float scale);
    bool hasTheme() { return kvxConfig.theme.rf; }
    const String& themePath() override { return kvxConfig.theme.paths.rf; }

private:
    void configMenu(void);
};

#endif
