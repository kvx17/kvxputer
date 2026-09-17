#ifndef __GPS_MENU_H__
#define __GPS_MENU_H__

#include <MenuItemInterface.h>

class GpsMenu : public MenuItemInterface {
public:
    GpsMenu() : MenuItemInterface("GPS") {}

    void optionsMenu(void);
    void wardrivingMenu(void);
    void configMenu(void);
    void drawIcon(float scale);
    bool hasTheme() { return kvxConfig.theme.gps; }
    const String& themePath() override { return kvxConfig.theme.paths.gps; }
};

#endif
